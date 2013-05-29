/* pdp11_dup.c: PDP-11 DUP11/DPV11 bit synchronous interface

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

   dup          DUP11 Unibus/DPV11 Qbus bit synchronous interface

   This module implements a bit synchronous interface to support DDCMP.  Other
   synchronous protocols which may have been supported on the DUP11/DPV11 bit 
   synchronous interface are explicitly not supported.

   Connections are modeled with a tcp session with connection management and 
   I/O provided by the tmxr library.

   The wire protocol implemented is native DDCMP WITHOUT the DDCMP SYNC 
   characters both initially and between DDCMP packets.

   15-May-13    MP      Initial implementation
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif

#include "sim_tmxr.h"
#include <ctype.h>

#if !defined(DUP_LINES)
#define DUP_LINES 8
#endif
#define INITIAL_DUP_LINES 1

#define DUP_WAIT   50           /* Minimum character time */
#define DUP_CONNECT_POLL    2   /* Seconds */

extern int32 IREQ (HLVL);
extern int32 tmxr_poll;                                 /* calibrated delay */
extern int32 clk_tps;                                   /* clock ticks per second */
extern int32 tmr_poll;                                  /* instructions per tick */

uint16 dup_rxcsr[DUP_LINES];
uint16 dup_rxdbuf[DUP_LINES];
uint16 dup_parcsr[DUP_LINES];
uint16 dup_txcsr[DUP_LINES];
uint16 dup_txdbuf[DUP_LINES];
uint32 dup_rxi = 0;                                     /* rcv interrupts */
uint32 dup_txi = 0;                                     /* xmt interrupts */
uint32 dup_wait[DUP_LINES];                             /* rcv/xmt byte delay */
uint32 dup_speed[DUP_LINES];                            /* line speed (bits/sec) */
uint8 *dup_rcvpacket[DUP_LINES];                        /* rcv buffer */
uint16 dup_rcvpksize[DUP_LINES];                        /* rcv buffer size */
uint16 dup_rcvpkoffset[DUP_LINES];                      /* rcv buffer offset */
uint16 dup_rcvpkinoff[DUP_LINES];                       /* rcv packet in offset */
uint8 *dup_xmtpacket[DUP_LINES];                        /* xmt buffer */
uint16 dup_xmtpksize[DUP_LINES];                        /* xmt buffer size */
uint16 dup_xmtpkoffset[DUP_LINES];                      /* xmt buffer offset */
uint16 dup_xmtpkoutoff[DUP_LINES];                      /* xmt packet out offset */
t_bool dup_xmtpkrdy[DUP_LINES];                         /* xmt packet ready */


t_stat dup_rd (int32 *data, int32 PA, int32 access);
t_stat dup_wr (int32 data, int32 PA, int32 access);
t_stat dup_set_modem (int32 dup, int32 rxcsr_bits);
t_stat dup_get_modem (int32 dup);
t_stat dup_svc (UNIT *uptr);
t_stat dup_poll_svc (UNIT *uptr);
t_stat dup_rcv_byte (int32 dup);
t_stat dup_reset (DEVICE *dptr);
t_stat dup_attach (UNIT *uptr, char *ptr);
t_stat dup_detach (UNIT *uptr);
t_stat dup_clear (int32 dup, t_bool flag);
void ddcmp_packet_trace (DEVICE *dptr, const char *txt, const uint8 *msg, int32 len, t_bool detail);
int32 dup_rxinta (void);
int32 dup_txinta (void);
void dup_update_rcvi (void);
void dup_update_xmti (void);
void dup_clr_rxint (int32 dup);
void dup_set_rxint (int32 dup);
void dup_clr_txint (int32 dup);
void dup_set_txint (int32 dup);
t_stat dup_setnl (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat dup_setspeed (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dup_showspeed (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dup_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat dup_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *dup_description (DEVICE *dptr);
static uint16 dup_crc16(uint16 crc, const void* vbuf, size_t len);

/* DDCMP packet types */

#define DDCMP_SOH 0201u   /* Numbered Data Message Identifier */
#define DDCMP_ENQ 0005u   /* Control Message Identifier */
#define DDCMP_DLE 0220u   /* Maintenance Message Identifier */

/* RXCSR - 16XXX0 - receiver control/status register */

BITFIELD dup_rxcsr_bits[] = {
    BIT(BDATSET),                           /* Data Set Change B */
#define RXCSR_V_BDATSET 0
#define RXCSR_M_BDATSET (1<<RXCSR_V_BDATSET)
    BIT(DTR),                               /* Data Terminal Ready */
#define RXCSR_V_DTR     1
#define RXCSR_M_DTR     (1<<RXCSR_V_DTR)
    BIT(RTS),                               /* Request To Send */
#define RXCSR_V_RTS     2
#define RXCSR_M_RTS     (1<<RXCSR_V_RTS)
    BIT(SECXMT),                            /* Secondary Transmit Data */
#define RXCSR_V_SECXMT  3
#define RXCSR_M_SECXMT  (1<<RXCSR_V_SECXMT)
    BIT(RCVEN),                             /* Receiver Enable */
#define RXCSR_V_RCVEN   4
#define RXCSR_M_RCVEN   (1<<RXCSR_V_RCVEN)
    BIT(DSCIE),                             /* Data Set Change Interrupt Enable */
#define RXCSR_V_DSCIE   5
#define RXCSR_M_DSCIE   (1<<RXCSR_V_DSCIE)
    BIT(RXIE),                              /* Receive Interrupt Enable */
#define RXCSR_V_RXIE    6
#define RXCSR_M_RXIE    (1<<RXCSR_V_RXIE)
    BIT(RXDONE),                            /* Receive Done */
#define RXCSR_V_RXDONE  7
#define RXCSR_M_RXDONE  (1<<RXCSR_V_RXDONE)
    BIT(STRSYN),                            /* Strip Sync */
#define RXCSR_V_STRSYN  8
#define RXCSR_M_STRSYN  (1<<RXCSR_V_STRSYN)
    BIT(DSR),                               /* Data Set Ready */
#define RXCSR_V_DSR     9
#define RXCSR_M_DSR     (1<<RXCSR_V_DSR)
    BIT(SECRCV),                            /* Secondary Receive Data */
#define RXCSR_V_SECRCV  10
#define RXCSR_M_SECRCV  (1<<RXCSR_V_SECRCV)
    BIT(RXACT),                             /* Receive Active */
#define RXCSR_V_RXACT   11
#define RXCSR_M_RXACT   (1<<RXCSR_V_RXACT)
    BIT(DCD),                               /* Carrier */
#define RXCSR_V_DCD     12
#define RXCSR_M_DCD     (1<<RXCSR_V_DCD)
    BIT(CTS),                               /* Clear to Send */
#define RXCSR_V_CTS     13
#define RXCSR_M_CTS     (1<<RXCSR_V_CTS)
    BIT(RING),                              /* Ring */
#define RXCSR_V_RING    14
#define RXCSR_M_RING    (1<<RXCSR_V_RING)
    BIT(DSCHNG),                            /* Data Set Change */
#define RXCSR_V_DSCHNG  15
#define RXCSR_M_DSCHNG  (1<<RXCSR_V_DSCHNG)
    ENDBITS
};
#define RXCSR_A_MODEM_BITS (RXCSR_M_RING | RXCSR_M_CTS | RXCSR_M_DCD)
#define RXCSR_B_MODEM_BITS (RXCSR_M_DSR)
#define RXCSR_WRITEABLE (RXCSR_M_STRSYN|RXCSR_M_RXIE|RXCSR_M_DSCIE|RXCSR_M_RCVEN|RXCSR_M_SECXMT|RXCSR_M_RTS|RXCSR_M_DTR)

/* RXDBUF - 16XXX2 - receiver Data Buffer register */

BITFIELD dup_rxdbuf_bits[] = {
    BITF(RXDBUF,8),                         /* Receive Data Buffer */
#define RXDBUF_V_RXDBUF  0
#define RXDBUF_S_RXDBUF  8
#define RXDBUF_M_RXDBUF  (((1<<RXDBUF_S_RXDBUF)-1)<<RXDBUF_V_RXDBUF)
    BIT(RSTRMSG),                           /* Receiver Start of Message */
#define RXDBUF_V_RSTRMSG 8
#define RXDBUF_M_RSTRMSG (1<<RXDBUF_V_RSTRMSG)
    BIT(RENDMSG),                           /* Receiver End Of Message */
#define RXDBUF_V_RENDMSG 9
#define RXDBUF_M_RENDMSG (1<<RXDBUF_V_RENDMSG)
    BIT(RABRT),                             /* Receiver Abort */
#define RXDBUF_V_RABRT   10
#define RXDBUF_M_RABRT   (1<<RXDBUF_V_RABRT)
    BITNCF(1),                              /* reserved */
    BIT(RCRCER),                            /* Receiver CRC Error */
#define RXDBUF_V_RCRCER  12
#define RXDBUF_M_RCRCER  (1<<RXDBUF_V_RCRCER)
    BITNCF(1),                              /* reserved */
    BIT(RXOVR),                             /* Receiver Overrun */
#define RXDBUF_V_RXOVR   14
#define RXDBUF_M_RXOVR   (1<<RXDBUF_V_RXOVR)
    BIT(RXERR),                             /* Receiver Error */
#define RXDBUF_V_RXERR   15
#define RXDBUF_M_RXERR   (1<<RXDBUF_V_RXERR)
    ENDBITS
};
#define RXDBUF_MBZ ((1<<13)|(1<<11))

/* PARCSR - 16XXX2 - Parameter Control/Status register */

BITFIELD dup_parcsr_bits[] = {
    BITF(ADSYNC,8),                         /* Secondart Station Address/Receiver Sync Char */
#define PARCSR_V_ADSYNC  0
#define PARCSR_S_ADSYNC  8
#define PARCSR_M_ADSYNC  (((1<<PARCSR_S_ADSYNC)-1)<<PARCSR_V_ADSYNC)
    BITNCF(1),                              /* reserved */
    BIT(NOCRC),                             /* No CRC */
#define PARCSR_V_NOCRC   9
#define PARCSR_M_NOCRC   (1<<PARCSR_V_NOCRC)
    BITNCF(2),                              /* reserved */
    BIT(SECMODE),                           /* Secondary Mode Select */
#define PARCSR_V_SECMODE 12
#define PARCSR_M_SECMODE (1<<PARCSR_V_SECMODE)
    BITNCF(2),                              /* reserved */
    BIT(DECMODE),                           /* DEC Mode */
#define PARCSR_V_DECMODE 15
#define PARCSR_M_DECMODE (1<<PARCSR_V_DECMODE)
    ENDBITS
};
#define PARCSR_MBZ ((1<<14)|(1<<13)|(1<<11)|(1<<10)|(1<<8))
#define PARCSR_WRITEABLE (PARCSR_M_DECMODE|PARCSR_M_SECMODE|PARCSR_M_NOCRC|PARCSR_M_ADSYNC)

/* TXCSR - 16XXX4 - Transmitter Control/Status register */

BITFIELD dup_txcsr_bits[] = {
    BITNCF(3),                              /* reserved */
    BIT(HALFDUP),                           /* Half Duplex */
#define TXCSR_V_HALFDUP  3
#define TXCSR_M_HALFDUP  (1<<TXCSR_V_HALFDUP)
    BIT(SEND),                              /* Enable Transmit */
#define TXCSR_V_SEND     4
#define TXCSR_M_SEND     (1<<TXCSR_V_SEND)
    BITNCF(1),                              /* reserved */
    BIT(TXIE),                              /* Transmit Interrupt Enable */
#define TXCSR_V_TXIE     6
#define TXCSR_M_TXIE     (1<<TXCSR_V_TXIE)
    BIT(TXDONE),                            /* Transmit Done */
#define TXCSR_V_TXDONE   7
#define TXCSR_M_TXDONE   (1<<TXCSR_V_TXDONE)
    BIT(DRESET),                            /* Device Reset */
#define TXCSR_V_DRESET   8
#define TXCSR_M_DRESET   (1<<TXCSR_V_DRESET)
    BIT(TXACT),                             /* Transmit Active */
#define TXCSR_V_TXACT    9
#define TXCSR_M_TXACT    (1<<TXCSR_V_TXACT)
    BIT(MAIDATA),                           /* Maintenance Mode Data Bit */
#define TXCSR_V_MAIDATA  10
#define TXCSR_M_MAIDATA  (1<<TXCSR_V_MAIDATA)
    BITF(MAISEL,2),                         /* Maintenance Select B and A */
#define TXCSR_V_MAISEL   11
#define TXCSR_S_MAISEL   2
#define TXCSR_M_MAISEL   (((1<<TXCSR_S_MAISEL)-1)<<TXCSR_V_MAISEL)
#define TXCSR_GETMAISEL(x) (((x) & TXCSR_M_MAISEL) >> TXCSR_V_MAISEL)
    BIT(MAISSCLK),                          /* Maintenance Single Step Clock */
#define TXCSR_V_MAISSCLK 13
#define TXCSR_M_MAISSCLK (1<<TXCSR_V_MAISSCLK)
    BIT(TXMNTOUT),                          /* Transmit Maint Data Out */
#define TXCSR_V_TXMNTOUT 14
#define TXCSR_M_TXMNTOUT (1<<TXCSR_V_TXMNTOUT)
    BIT(TXDLAT),                            /* Transmit Data Late */
#define TXCSR_V_TXDLAT   15
#define TXCSR_M_TXDLAT   (1<<TXCSR_V_TXDLAT)
    ENDBITS
};
#define TXCSR_MBZ ((1<<5)|(1<<2)|(1<<1)|(1<<0))
#define TXCSR_WRITEABLE (TXCSR_M_MAISSCLK|TXCSR_M_MAISEL|TXCSR_M_MAIDATA|TXCSR_M_DRESET|TXCSR_M_TXIE|TXCSR_M_SEND|TXCSR_M_HALFDUP)

/* TXDBUF - 16XXX6 - transmitter Data Buffer register */

BITFIELD dup_txdbuf_bits[] = {
    BITF(TXDBUF,8),                         /* Transmit Data Buffer */
#define TXDBUF_V_TXDBUF  0
#define TXDBUF_S_TXDBUF  8
#define TXDBUF_M_TXDBUF  (((1<<TXDBUF_S_TXDBUF)-1)<<TXDBUF_V_TXDBUF)
    BIT(TSOM),                              /* Transmit Start of Message */
#define TXDBUF_V_TSOM    8
#define TXDBUF_M_TSOM    (1<<TXDBUF_V_TSOM)
    BIT(TEOM),                              /* End of Transmitted Message */
#define TXDBUF_V_TEOM    9
#define TXDBUF_M_TEOM    (1<<TXDBUF_V_TEOM)
    BIT(TABRT),                             /* Transmit Abort */
#define TXDBUF_V_TABRT   10
#define TXDBUF_M_TABRT   (1<<TXDBUF_V_TABRT)
    BIT(MAINTT),                            /* Maintenance Timer */
#define TXDBUF_V_MAINTT  11
#define TXDBUF_M_MAINTT  (1<<TXDBUF_V_MAINTT)
    BIT(TCRCTIN),                           /* Transmit CSR Input */
#define TXDBUF_V_TCRCTIN 12
#define TXDBUF_M_TCRCTIN (1<<TXDBUF_V_TCRCTIN)
    BITNCF(1),                              /* reserved */
    BIT(RCRCTIN),                           /* Receive CSR Input */
#define TXDBUF_V_RCRCTIN 14
#define TXDBUF_M_RCRCTIN (1<<TXDBUF_V_RCRCTIN)
    BITNCF(1),                              /* reserved */
    ENDBITS
};
#define TXDBUF_MBZ ((1<<15)|(1<<13))
#define TXDBUF_WRITEABLE (TXDBUF_M_TABRT|TXDBUF_M_TEOM|TXDBUF_M_TSOM|TXDBUF_M_TXDBUF)


/* DUP data structures

   dup_dev      DUP device descriptor
   dup_unit     DUP unit descriptor
   dup_reg      DUP register list
*/

#define IOLN_DUP        010

DIB dup_dib = {
    IOBA_AUTO,
    IOLN_DUP * INITIAL_DUP_LINES,
    &dup_rd,    /* read */
    &dup_wr,    /* write */
    2,          /* # of vectors */
    IVCL (DUPRX),
    VEC_AUTO,
    { &dup_rxinta, &dup_txinta }/* int. ack. routines */
};

UNIT dup_unit_template = {
    UDATA (&dup_svc, UNIT_ATTABLE, 0), 
    };

UNIT dup_poll_unit_template = {
    UDATA (&dup_poll_svc, UNIT_DIS, 0), 
    };

UNIT dup_units[DUP_LINES+1];    /* One unit per line and a polling unit */

REG dup_reg[] = {
    { BRDATADF (RXCSR,          dup_rxcsr,  DEV_RDX, 16, DUP_LINES, "receive control/status register",  dup_rxcsr_bits) },
    { BRDATADF (RXDBUF,        dup_rxdbuf,  DEV_RDX, 16, DUP_LINES, "receive data buffer",              dup_rxdbuf_bits) },
    { BRDATADF (PARCSR,        dup_parcsr,  DEV_RDX, 16, DUP_LINES, "receive control/status register",  dup_parcsr_bits) },
    { BRDATADF (TXCSR,          dup_txcsr,  DEV_RDX, 16, DUP_LINES, "transmit control/status register", dup_txcsr_bits) },
    { BRDATADF (TXDBUF,        dup_txdbuf,  DEV_RDX, 16, DUP_LINES, "transmit data buffer",             dup_txdbuf_bits) },
    { GRDATAD  (RXINT,            dup_rxi,  DEV_RDX, DUP_LINES,  0, "receive interrupts") },
    { GRDATAD  (TXINT,            dup_txi,  DEV_RDX, DUP_LINES,  0, "transmit interrupts") },
    { BRDATAD  (RXWAIT,          dup_wait,       10, 24, DUP_LINES, "delay time for transmit/receive bytes") },
    { BRDATAD  (RPOFFSET, dup_rcvpkoffset,  DEV_RDX, 16, DUP_LINES, "receive assembly packet offset") },
    { BRDATAD  (TPOFFSET, dup_xmtpkoffset,  DEV_RDX, 16, DUP_LINES, "transmit assembly packet offset") },
    { BRDATAD  (RPINOFF,   dup_rcvpkinoff,  DEV_RDX, 16, DUP_LINES, "receive digest packet offset") },
    { BRDATAD  (TPOUTOFF, dup_xmtpkoutoff,  DEV_RDX, 16, DUP_LINES, "transmit digest packet offset") },
    { BRDATAD  (TPREADY,     dup_xmtpkrdy,  DEV_RDX, 16, DUP_LINES, "transmit packet ready") },
    { NULL }
    };

TMLN *dup_ldsc = NULL;                                  /* line descriptors */
TMXR dup_desc = { INITIAL_DUP_LINES, 0, 0, NULL };      /* mux descriptor */

MTAB dup_mod[] = {
    { MTAB_XTD|MTAB_VUN,          0, "SPEED", "SPEED=bits/sec (0=unrestricted)" ,
        &dup_setspeed, &dup_showspeed, NULL, "Display rate limit" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, "VECTOR", "VECTOR",
        &set_vec, &show_vec_mux, (void *) &dup_desc, "Interrupt vector" },
#if !defined (VM_PDP10)
    { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
        &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
#endif
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dup_setnl, &tmxr_show_lines, (void *) &dup_desc, "Display number of lines" },
    { 0 }
    };

/* debugging bitmaps */
#define DBG_REG  0x0001                                 /* trace read/write registers */
#define DBG_INT  0x0002                                 /* display transfer requests */
#define DBG_PKT  0x0004                                 /* display packets */
#define DBG_XMT  TMXR_DBG_XMT                           /* display Transmitted Data */
#define DBG_RCV  TMXR_DBG_RCV                           /* display Received Data */
#define DBG_MDM  TMXR_DBG_MDM                           /* display Modem Signals */
#define DBG_CON  TMXR_DBG_CON                           /* display connection activities */
#define DBG_TRC  TMXR_DBG_TRC                           /* display trace routine calls */
#define DBG_ASY  TMXR_DBG_ASY                           /* display Asynchronous Activities */

DEBTAB dup_debug[] = {
  {"REG",    DBG_REG},
  {"INT",    DBG_INT},
  {"PKT",    DBG_PKT},
  {"XMT",    DBG_XMT},
  {"RCV",    DBG_RCV},
  {"MDM",    DBG_MDM},
  {"CON",    DBG_CON},
  {"TRC",    DBG_TRC},
  {"ASY",    DBG_ASY},
  {0}
};

/*
   We have two devices defined here (dup_dev and dpv_dev) which have the 
   same units.  This would normally never be allowed since two devices can't
   actually share units.  This problem is avoided in this case since both 
   devices start out as disabled and the logic in dup_reset allows only 
   one of these devices to be enabled at a time.  The DUP device is allowed 
   on Unibus systems and the DPV device Qbus systems.
   This monkey business is necessary due to the fact that although both
   the DUP and DPV devices have almost the same functionality and almost
   the same register programming interface, they are different enough that
   they fall in different priorities in the autoconfigure address and vector
   rules.
   This 'shared' unit model therefore means we can't call the 
   find_dev_from_unit api to uniquely determine the device structure.  
   We define the DUPDPTR macro to return the active device pointer when 
   necessary.
 */
DEVICE dup_dev = {
    "DUP", dup_units, dup_reg, dup_mod,
    2, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &dup_reset,
    NULL, &dup_attach, &dup_detach,
    &dup_dib, DEV_DIS | DEV_DISABLE | DEV_UBUS | DEV_DEBUG, 0,
    dup_debug, NULL, NULL, &dup_help, dup_help_attach, &dup_desc, 
    &dup_description
    };

DEVICE dpv_dev = {
    "DPV", dup_units, dup_reg, dup_mod,
    2, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &dup_reset,
    NULL, &dup_attach, &dup_detach,
    &dup_dib, DEV_DIS | DEV_DISABLE | DEV_QBUS | DEV_DEBUG, 0,
    dup_debug, NULL, NULL, &dup_help, dup_help_attach, &dup_desc, 
    &dup_description
    };

#define DUPDPTR ((UNIBUS) ? &dup_dev : &dpv_dev)

/* Register names for Debug tracing */
static char *dup_rd_regs[] =
    {"RXCSR ", "RXDBUF", "TXCSR ", "TXDBUF" };
static char *dup_wr_regs[] = 
    {"RXCSR ", "PARCSR", "TXCSR ", "TXDBUF"};


/* DUP11/DPV11 bit synchronous interface routines

   dup_rd       I/O page read
   dup_wr       I/O page write
   dup_svc      process event
   dup_poll_svc process polling events
   dup_reset    process reset
   dup_setnl    set number of lines
   dup_attach   process attach
   dup_detach   process detach
*/

t_stat dup_rd (int32 *data, int32 PA, int32 access)
{
static BITFIELD* bitdefs[] = {dup_rxcsr_bits, dup_rxdbuf_bits, dup_txcsr_bits, dup_txdbuf_bits};
static uint16 *regs[] = {dup_rxcsr, dup_rxdbuf, dup_txcsr, dup_txdbuf};
int32 dup = ((PA - dup_dib.ba) >> 3);                   /* get line num */
TMLN *lp = &dup_desc.ldsc[dup];
int32 orig_val;

if (dup >= dup_desc.lines)                              /* validate line number */
    return SCPE_IERR;

orig_val = regs[(PA >> 1) & 03][dup];
switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

    case 00:                                            /* RXCSR */
        dup_get_modem (dup);
        *data = dup_rxcsr[dup];
        break;

    case 01:                                            /* RXDBUF */
        *data = dup_rxdbuf[dup];
        dup_rxcsr[dup] &= ~RXCSR_M_RXDONE;
        if (dup_rxcsr[dup] & RXCSR_M_RXACT)
            sim_activate (dup_units+dup, dup_wait[dup]);
        break;

    case 02:                                            /* TXCSR */
        *data = dup_txcsr[dup];
        break;

    case 03:                                            /* TXDBUF */
        *data = dup_txdbuf[dup];
        break;
    }

sim_debug(DBG_REG, DUPDPTR, "dup_rd(PA=0x%08X [%s], data=0x%X) ", PA, dup_rd_regs[(PA >> 1) & 03], *data);
sim_debug_bits(DBG_REG, DUPDPTR, bitdefs[(PA >> 1) & 03], (uint32)(orig_val), (uint32)(regs[(PA >> 1) & 03][dup]), TRUE);

return SCPE_OK;
}

t_stat dup_wr (int32 data, int32 PA, int32 access)
{
static BITFIELD* bitdefs[] = {dup_rxcsr_bits, dup_parcsr_bits, dup_txcsr_bits, dup_txdbuf_bits};
static uint16 *regs[] = {dup_rxcsr, dup_parcsr, dup_txcsr, dup_txdbuf};
int32 dup = ((PA - dup_dib.ba) >> 3);                   /* get line num */
int32 orig_val;

if (dup >= dup_desc.lines)                              /* validate line number */
    return SCPE_IERR;

orig_val = regs[(PA >> 1) & 03][dup];

switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

    case 00:                                            /* RXCSR */
        dup_set_modem (dup, data);
        dup_rxcsr[dup] &= ~RXCSR_WRITEABLE;
        dup_rxcsr[dup] |= (data & RXCSR_WRITEABLE);
        if ((dup_rxcsr[dup] & RXCSR_M_RTS) &&           /* Upward transition of RTS */
            (!(orig_val & RXCSR_M_RTS)))                /* Enables Receive on the line */
            dup_desc.ldsc[dup].rcve = TRUE;
        if ((dup_rxcsr[dup] & RXCSR_M_RTS) &&           /* Upward transition of RTS */
            (!(orig_val & RXCSR_M_RTS)) &&              /* while receiver is enabled and */
            (dup_rxcsr[dup] & RXCSR_M_RCVEN) &&         /* not stripping sync characters */
            (!(dup_rxcsr[dup] & RXCSR_M_STRSYN)) ) {    /* Receive a SYNC character */
            dup_rxcsr[dup] |= RXCSR_M_RXDONE;
            dup_rxdbuf[dup] &= ~RXDBUF_M_RXDBUF;
            dup_rxdbuf[dup] |= (dup_parcsr[dup] & PARCSR_M_ADSYNC);
            if (dup_rxcsr[dup] & RXCSR_M_RXIE)
                dup_set_rxint (dup);
            }
        if ((dup_rxcsr[dup] & RXCSR_M_RCVEN) && 
            (!(orig_val & RXCSR_M_RCVEN))) {            /* Upward transition of receiver enable */
            dup_rcv_byte (dup);                         /* start any pending receive */
            }
        if ((!(dup_rxcsr[dup] & RXCSR_M_RCVEN)) && 
            (orig_val & RXCSR_M_RCVEN)) {               /* Downward transition of receiver enable */
            dup_rxcsr[dup] &= ~RXCSR_M_RXDONE;
            if ((dup_rcvpkinoff[dup] != 0) || 
                (dup_rcvpkoffset[dup] != 0))
                dup_rcvpkinoff[dup] = dup_rcvpkoffset[dup] = 0;
            }
        break;

    case 01:                                            /* PARCSR */
        dup_parcsr[dup] &= ~PARCSR_WRITEABLE;
        dup_parcsr[dup] |= (data & PARCSR_WRITEABLE);
        break;

    case 02:                                            /* TXCSR */
        dup_txcsr[dup] &= ~TXCSR_WRITEABLE;
        dup_txcsr[dup] |= (data & TXCSR_WRITEABLE);
        if ((!(dup_txcsr[dup] & TXCSR_M_SEND)) && (orig_val & TXCSR_M_SEND))
            dup_txcsr[dup] &= ~TXCSR_M_TXACT;
        if (dup_txcsr[dup] & TXCSR_M_DRESET) {
            dup_clear(dup, FALSE);
            break;
            }
        break;

    case 03:                                            /* TXDBUF */
        dup_txdbuf[dup] &= ~TXDBUF_WRITEABLE;
        dup_txdbuf[dup] |= (data & TXDBUF_WRITEABLE);
        dup_txcsr[dup] &= ~TXCSR_M_TXDONE;
        if (dup_txcsr[dup] & TXCSR_M_SEND) {
            dup_txcsr[dup] |= TXCSR_M_TXACT;
            sim_activate (dup_units+dup, dup_wait[dup]);
            }
        break;
    }

sim_debug(DBG_REG, DUPDPTR, "dup_wr(PA=0x%08X [%s], data=0x%X) ", PA, dup_wr_regs[(PA >> 1) & 03], data);
sim_debug_bits(DBG_REG, DUPDPTR, bitdefs[(PA >> 1) & 03], (uint32)orig_val, (uint32)regs[(PA >> 1) & 03][dup], TRUE);
return SCPE_OK;
}

t_stat dup_set_modem (int32 dup, int32 rxcsr_bits)
{
int32 bits_to_set, bits_to_clear;

if ((rxcsr_bits & (RXCSR_M_DTR | RXCSR_M_RTS)) == (dup_rxcsr[dup] & (RXCSR_M_DTR | RXCSR_M_RTS)))
    return SCPE_OK;
bits_to_set = ((rxcsr_bits & RXCSR_M_DTR) ? TMXR_MDM_DTR : 0) | ((rxcsr_bits & RXCSR_M_RTS) ? TMXR_MDM_RTS : 0);
bits_to_clear = (~bits_to_set) & (TMXR_MDM_DTR | TMXR_MDM_RTS);
tmxr_set_get_modem_bits (dup_desc.ldsc+dup, bits_to_set, bits_to_clear, NULL);
return SCPE_OK;
}

t_stat dup_get_modem (int32 dup)
{
int32 modem_bits;
int32 old_rxcsr_a_modem_bits, new_rxcsr_a_modem_bits, old_rxcsr_b_modem_bits, new_rxcsr_b_modem_bits;
TMLN *lp = &dup_desc.ldsc[dup];

old_rxcsr_a_modem_bits = dup_rxcsr[dup] & RXCSR_A_MODEM_BITS;
old_rxcsr_b_modem_bits = dup_rxcsr[dup] & RXCSR_B_MODEM_BITS;
tmxr_set_get_modem_bits (lp, 0, 0, &modem_bits);
new_rxcsr_a_modem_bits = (((modem_bits & TMXR_MDM_RNG) ? RXCSR_M_RING : 0) |
                          ((modem_bits & TMXR_MDM_DCD) ? RXCSR_M_DCD : 0) |
                          ((modem_bits & TMXR_MDM_CTS) ? RXCSR_M_CTS : 0));
new_rxcsr_b_modem_bits = ((modem_bits & TMXR_MDM_DSR) ? RXCSR_M_DSR : 0);
dup_rxcsr[dup] &= ~(RXCSR_A_MODEM_BITS | RXCSR_B_MODEM_BITS);
dup_rxcsr[dup] |= new_rxcsr_a_modem_bits | new_rxcsr_b_modem_bits;
if (old_rxcsr_a_modem_bits != new_rxcsr_a_modem_bits)
    dup_rxcsr[dup] |= RXCSR_M_DSCHNG;
else
    dup_rxcsr[dup] &= ~RXCSR_M_DSCHNG;
if (old_rxcsr_b_modem_bits != new_rxcsr_b_modem_bits)
    dup_rxcsr[dup] |= RXCSR_M_BDATSET;
else
    dup_rxcsr[dup] &= ~RXCSR_M_BDATSET;
if ((dup_rxcsr[dup] & RXCSR_M_DSCHNG) &&
    (dup_rxcsr[dup] & RXCSR_M_DSCIE))
    dup_set_rxint (dup);
return SCPE_OK;
}

t_stat dup_rcv_byte (int32 dup)
{
sim_debug (DBG_TRC, DUPDPTR, "dup_rcv_byte(dup=%d) - %s, byte %d of %d\n", dup, 
           (dup_rxcsr[dup] & RXCSR_M_RCVEN) ? "enabled" : "disabled",
           dup_rcvpkinoff[dup], dup_rcvpkoffset[dup]);
if (!(dup_rxcsr[dup] & RXCSR_M_RCVEN) || (dup_rcvpkoffset[dup] == 0))
    return SCPE_OK;
dup_rxcsr[dup] |= RXCSR_M_RXACT;
dup_rxdbuf[dup] &= ~RXDBUF_M_RCRCER;
dup_rxdbuf[dup] &= ~RXDBUF_M_RXDBUF;
dup_rxdbuf[dup] |= dup_rcvpacket[dup][dup_rcvpkinoff[dup]++];
dup_rxcsr[dup] |= RXCSR_M_RXDONE;
if (((dup_rcvpkinoff[dup] == 8) || 
     (dup_rcvpkinoff[dup] >= dup_rcvpkoffset[dup])) &&
    (0 == dup_crc16 (0, dup_rcvpacket[dup], dup_rcvpkinoff[dup])))
    dup_rxdbuf[dup] |= RXDBUF_M_RCRCER;
else
    dup_rxdbuf[dup] &= ~RXDBUF_M_RCRCER;
if (dup_rcvpkinoff[dup] >= dup_rcvpkoffset[dup]) {
    dup_rcvpkinoff[dup] = dup_rcvpkoffset[dup] = 0;
    dup_rxcsr[dup] &= ~RXCSR_M_RXACT;
    }
if (dup_rxcsr[dup] & RXCSR_M_RXIE)
    dup_set_rxint (dup);
return SCPE_OK;
}


/* service routine to delay device activity */
t_stat dup_svc (UNIT *uptr)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);
TMLN *lp = &dup_desc.ldsc[dup];

sim_debug(DBG_TRC, DUPDPTR, "dup_svc(dup=%d)\n", dup);
if (!(dup_txcsr[dup] & TXCSR_M_TXDONE) && (!dup_xmtpkrdy[dup])) {
    if (dup_txdbuf[dup] & TXDBUF_M_TSOM) {
        dup_xmtpkoffset[dup] = 0;
        }
    else {
        if ((dup_xmtpkoffset[dup] != 0) ||
            ((dup_txdbuf[dup] & TXDBUF_M_TXDBUF) != (dup_parcsr[dup] & PARCSR_M_ADSYNC))) {
            if (!(dup_txdbuf[dup] & TXDBUF_M_TEOM)) {
                if (dup_xmtpkoffset[dup] + 1 > dup_xmtpksize[dup]) {
                    dup_xmtpksize[dup] += 512;
                    dup_xmtpacket[dup] = realloc (dup_xmtpacket[dup], dup_xmtpksize[dup]);
                    }
                dup_xmtpacket[dup][dup_xmtpkoffset[dup]] = dup_txdbuf[dup] & TXDBUF_M_TXDBUF;
                dup_xmtpkoffset[dup] += 1;
                }
            }
        }
    dup_txcsr[dup] |= TXCSR_M_TXDONE;
    if (dup_txcsr[dup] & TXCSR_M_TXIE)
        dup_set_txint (dup);
    if (dup_txdbuf[dup] & TXDBUF_M_TEOM) { /* Packet ready to send? */
        uint16 crc16 = dup_crc16 (0, dup_xmtpacket[dup], dup_xmtpkoffset[dup]);

        if (dup_xmtpkoffset[dup] + 2 > dup_xmtpksize[dup]) {
            dup_xmtpksize[dup] += 512;
            dup_xmtpacket[dup] = realloc (dup_xmtpacket[dup], dup_xmtpksize[dup]);
            }
        dup_xmtpacket[dup][dup_xmtpkoffset[dup]++] = crc16 & 0xFF;
        dup_xmtpacket[dup][dup_xmtpkoffset[dup]++] = crc16 >> 8;
        sim_debug(DBG_TRC, DUPDPTR, "dup_svc(dup=%d) - Packet Done %d bytes\n", dup, dup_xmtpkoffset[dup]);
        ddcmp_packet_trace (DUPDPTR, ">>> XMT Packet", dup_xmtpacket[dup], dup_xmtpkoffset[dup], TRUE);
        dup_xmtpkoutoff[dup] = 0;
        dup_xmtpkrdy[dup] = TRUE;
        }
    }
if (dup_xmtpkrdy[dup] && lp->xmte) {
    t_stat st = SCPE_OK;

    while ((st == SCPE_OK) && (dup_xmtpkoutoff[dup] < dup_xmtpkoffset[dup])) {
        st = tmxr_putc_ln (lp, dup_xmtpacket[dup][dup_xmtpkoutoff[dup]]);
        if (st == SCPE_OK)
            ++dup_xmtpkoutoff[dup];
        }
    tmxr_send_buffered_data (lp);               /* send any buffered data */
    if (st == SCPE_LOST) {                      /* line state transition? */
        dup_get_modem (dup);
        dup_xmtpkrdy[dup] = FALSE;
        }
    else
        if (st == SCPE_OK) {
            sim_debug(DBG_PKT, DUPDPTR, "dup_svc(dup=%d) - %d byte packet transmission complete\n", dup, dup_xmtpkoutoff[dup]);
            dup_xmtpkrdy[dup] = FALSE;
            }
        else {
            sim_debug(DBG_PKT, DUPDPTR, "dup_svc(dup=%d) - Packet Transmission Stalled with %d bytes remaining\n", dup, (int)(dup_xmtpkoffset[dup]-dup_xmtpkoutoff[dup]));
            }
    if (!dup_xmtpkrdy[dup])
        dup_txcsr[dup] &= ~TXCSR_M_TXACT;
    }
if (dup_rxcsr[dup] & RXCSR_M_RXACT)
    dup_rcv_byte (dup);
return SCPE_OK;
}

t_stat dup_poll_svc (UNIT *uptr)
{
int32 dup, active, attached, c;

sim_debug(DBG_TRC, DUPDPTR, "dup_poll_svc()\n");

dup = tmxr_poll_conn(&dup_desc);
if (dup >= 0) {                                 /* new connection? */
    dup_rxcsr[dup] |= RXCSR_M_RING | ((dup_rxcsr[dup] & RXCSR_M_DTR) ? (RXCSR_M_DCD | RXCSR_M_CTS | RXCSR_M_DSR) : 0);
    dup_rxcsr[dup] |= RXCSR_M_DSCHNG;
    if (dup_rxcsr[dup] & RXCSR_M_DSCIE)
        dup_set_rxint (dup);                    /* Interrupt */
    }
tmxr_poll_rx (&dup_desc);
tmxr_poll_tx (&dup_desc);
for (dup=active=attached=0; dup < dup_desc.lines; dup++) {
    TMLN *lp = &dup_desc.ldsc[dup];

    if (dup_units[dup].flags & UNIT_ATT)
        ++attached;
    if (dup_ldsc[dup].conn)
        ++active;
    dup_get_modem (dup);
    if (lp->xmte && dup_xmtpkrdy[dup]) {
        sim_debug(DBG_PKT, DUPDPTR, "dup_poll_svc(dup=%d) - Packet Transmission of remaining %d bytes restarting...\n", dup, (int)(dup_xmtpkoffset[dup]-dup_xmtpkoutoff[dup]));
        dup_svc (&dup_units[dup]);              /* Flush pending output */
        }
    if (!(dup_rxcsr[dup] & RXCSR_M_RXACT)) {
        while (TMXR_VALID & (c = tmxr_getc_ln (lp))) {
            if (dup_rcvpkoffset[dup] + 1 > dup_rcvpksize[dup]) {
                dup_rcvpksize[dup] += 512;
                dup_rcvpacket[dup] = realloc (dup_rcvpacket[dup], dup_rcvpksize[dup]);
                }
            dup_rcvpacket[dup][dup_rcvpkoffset[dup]] = c;
            dup_rcvpkoffset[dup] += 1;
            if (dup_rcvpkoffset[dup] == 1) {    /* Validate first byte in packet */
                if ((dup_rxcsr[dup] & RXCSR_M_STRSYN) &&
                    (dup_rcvpacket[dup][0] == (dup_parcsr[dup] & PARCSR_M_ADSYNC))) {
                    dup_rcvpkoffset[dup] = 0;
                    continue;
                    }
                if (dup_parcsr[dup] & PARCSR_M_DECMODE) {
                    switch (dup_rcvpacket[dup][0]) {
                        default:
                            sim_debug (DBG_PKT, DUPDPTR, "Ignoring unexpected byte 0%o in DDCMP mode\n", dup_rcvpacket[dup][0]);
                            dup_rcvpkoffset[dup] = 0;
                        case DDCMP_SOH:
                        case DDCMP_ENQ:
                        case DDCMP_DLE:
                            continue;
                        }
                    }
                }
            if (dup_rcvpkoffset[dup] >= 8) {
                if (dup_rcvpacket[dup][0] == DDCMP_ENQ) { /* Control Message? */
                    ddcmp_packet_trace (DUPDPTR, "<<< RCV Packet", dup_rcvpacket[dup], dup_rcvpkoffset[dup], TRUE);
                    dup_rcvpkinoff[dup] = 0;
                    dup_rcv_byte (dup);
                    break;
                    }
                else {
                    int32 count = ((dup_rcvpacket[dup][2] & 0x3F) << 8)| dup_rcvpacket[dup][1];

                    if (dup_rcvpkoffset[dup] >= 10 + count) {
                        ddcmp_packet_trace (DUPDPTR, "<<< RCV Packet", dup_rcvpacket[dup], dup_rcvpkoffset[dup], TRUE);
                        dup_rcvpkinoff[dup] = 0;
                        dup_rcv_byte (dup);
                        break;
                        }
                    }
                }
            }
        }
    }
if (active)
    sim_clock_coschedule (uptr, tmxr_poll);     /* reactivate */
else {
    for (dup=0; dup < dup_desc.lines; dup++) {
        if (dup_speed[dup]/8) {
            dup_wait[dup] = (tmr_poll*clk_tps)/(dup_speed[dup]/8);
            if (dup_wait[dup] < DUP_WAIT)
                dup_wait[dup] = DUP_WAIT;
            }
        else
            dup_wait[dup] = DUP_WAIT; /* set minimum byte delay */
        }
    if (attached)
        sim_activate_after (uptr, DUP_CONNECT_POLL*1000000);/* periodic check for connections */
    }
return SCPE_OK;
}

/* Debug routines */

void ddcmp_packet_trace (DEVICE *dptr, const char *txt, const uint8 *msg, int32 len, t_bool detail)
{
if (sim_deb && dptr && (DBG_PKT & dptr->dctrl)) {
    sim_debug(DBG_PKT, dptr, "%s  len: %d\n", txt, len);
    if (detail) {
        int i, same, group, sidx, oidx;
        char outbuf[80], strbuf[18];
        static char hex[] = "0123456789ABCDEF";

        switch (msg[0]) {
            case DDCMP_SOH:   /* Data Message */
                sim_debug (DBG_PKT, dptr, "Data Message, Link: %d, Count: %d, Resp: %d, Num: %d, HDRCRC: %s, DATACRC: %s\n", msg[2]>>6, ((msg[2] & 0x3F) << 8)|msg[1], msg[3], msg[4], 
                                            (0 == dup_crc16 (0, msg, 8)) ? "OK" : "BAD", (0 == dup_crc16 (0, msg+8, 2+(((msg[2] & 0x3F) << 8)|msg[1]))) ? "OK" : "BAD");
                break;
            case DDCMP_ENQ:   /* Control Message */
                sim_debug (DBG_PKT, dptr, "Control: Type: %d ", msg[1]);
                switch (msg[1]) {
                    case 1: /* ACK */
                        sim_debug (DBG_PKT, dptr, "(ACK) ACKSUB: %d, Link: %d, Resp: %d\n", msg[2] & 0x3F, msg[2]>>6, msg[3]);
                        break;
                    case 2: /* NAK */
                        sim_debug (DBG_PKT, dptr, "(NAK) Reason: %d, Link: %d, Resp: %d\n", msg[2] & 0x3F, msg[2]>>6, msg[3]);
                        break;
                    case 3: /* REP */
                        sim_debug (DBG_PKT, dptr, "(REP) REPSUB: %d, Link: %d, Num: %d\n", msg[2] & 0x3F, msg[2]>>6, msg[4]);
                        break;
                    case 6: /* STRT */
                        sim_debug (DBG_PKT, dptr, "(STRT) STRTSUB: %d, Link: %d\n", msg[2] & 0x3F, msg[2]>>6);
                        break;
                    case 7: /* STACK */
                        sim_debug (DBG_PKT, dptr, "(STACK) STCKSUB: %d, Link: %d\n", msg[2] & 0x3F, msg[2]>>6);
                        break;
                    default: /* Unknown */
                        sim_debug (DBG_PKT, dptr, "(Unknown=0%o)\n", msg[1]);
                        break;
                    }
                if (len != 8)
                    sim_debug (DBG_PKT, dptr, "Unexpected Control Message Length: %d expected 8\n", len);
                if (0 != dup_crc16 (0, msg, len))
                    sim_debug (DBG_PKT, dptr, "Unexpected Message CRC\n");
                break;
            case DDCMP_DLE:   /* Maintenance Message */
                sim_debug (DBG_PKT, dptr, "Maintenance Message, Link: %d, Count: %d, HDRCRC: %s, DATACRC: %s\n", msg[2]>>6, ((msg[2] & 0x3F) << 8)| msg[1], 
                                            (0 == dup_crc16 (0, msg, 8)) ? "OK" : "BAD", (0 == dup_crc16 (0, msg+8, 2+(((msg[2] & 0x3F) << 8)| msg[1]))) ? "OK" : "BAD");
                break;
            }
        for (i=same=0; i<len; i += 16) {
            if ((i > 0) && (0 == memcmp(&msg[i], &msg[i-16], 16))) {
                ++same;
                continue;
                }
            if (same > 0) {
                sim_debug(DBG_PKT, dptr, "%04X thru %04X same as above\n", i-(16*same), i-1);
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
            sim_debug(DBG_PKT, dptr, "%04X%-48s %s\n", i, outbuf, strbuf);
            }
        if (same > 0) {
            sim_debug(DBG_PKT, dptr, "%04X thru %04X same as above\n", i-(16*same), len-1);
            }
        }
    }
}

/* Interrupt routines */

void dup_clr_rxint (int32 dup)
{
dup_rxi = dup_rxi & ~(1 << dup);                        /* clr mux rcv int */
if (dup_rxi == 0)                                       /* all clr? */
    CLR_INT (DUPRX);
else SET_INT (DUPRX);                                   /* no, set intr */
return;
}

void dup_set_rxint (int32 dup)
{
dup_rxi = dup_rxi | (1 << dup);                         /* set mux rcv int */
SET_INT (DUPRX);                                        /* set master intr */
sim_debug(DBG_INT, DUPDPTR, "dup_set_rxint(dup=%d)\n", dup);
return;
}

int32 dup_rxinta (void)
{
int32 dup;

for (dup = 0; dup < dup_desc.lines; dup++) {            /* find 1st mux */
    if (dup_rxi & (1 << dup)) {
        sim_debug(DBG_INT, DUPDPTR, "dup_rxinta(dup=%d)\n", dup);
        dup_clr_rxint (dup);                            /* clear intr */
        return (dup_dib.vec + (dup * 010));             /* return vector */
        }
    }
return 0;
}

void dup_clr_txint (int32 dup)
{
dup_txi = dup_txi & ~(1 << dup);                        /* clr mux xmt int */
if (dup_txi == 0)                                       /* all clr? */
    CLR_INT (DUPTX);
else SET_INT (DUPTX);                                   /* no, set intr */
return;
}

void dup_set_txint (int32 dup)
{
dup_txi = dup_txi | (1 << dup);                         /* set mux xmt int */
SET_INT (DUPTX);                                        /* set master intr */
sim_debug(DBG_INT, DUPDPTR, "dup_set_txint(dup=%d)\n", dup);
return;
}

int32 dup_txinta (void)
{
int32 dup;

for (dup = 0; dup < dup_desc.lines; dup++) {            /* find 1st mux */
    if (dup_txi & (1 << dup)) {
        sim_debug(DBG_INT, DUPDPTR, "dup_txinta(dup=%d)\n", dup);
        dup_clr_txint (dup);                            /* clear intr */
        return (dup_dib.vec + 4 + (dup * 010));         /* return vector */
        }
    }
return 0;
}

/* Device reset */

t_stat dup_clear (int32 dup, t_bool flag)
{
sim_debug(DBG_TRC, DUPDPTR, "dup_clear(dup=%d,flag=%d)\n", dup, flag);

dup_rxdbuf[dup] = 0;                                    /* silo empty */
dup_txdbuf[dup] = 0;
dup_parcsr[dup] = 0;                                    /* no params */
dup_txcsr[dup] = TXCSR_M_TXDONE;                        /* clear CSR */
dup_wait[dup] = DUP_WAIT;                               /* initial/default byte delay */
if (flag)                                               /* INIT? clr all */
    dup_rxcsr[dup] = 0;
else
    dup_rxcsr[dup] &= ~(RXCSR_M_DTR|RXCSR_M_RTS);       /* else save dtr */
dup_clr_rxint (dup);                                    /* clear int */
dup_clr_txint (dup);
if (!dup_ldsc[dup].conn)                                /* set xmt enb */
    dup_ldsc[dup].xmte = 1;
dup_ldsc[dup].rcve = 0;                                 /* clr rcv enb */
return SCPE_OK;
}

t_stat dup_reset (DEVICE *dptr)
{
int32 i, ndev;

sim_debug(DBG_TRC, dptr, "dup_reset()\n");

if ((UNIBUS) && (dptr == &dpv_dev)) {
    if (!(dptr->flags & DEV_DIS)) {
        printf ("Can't enable Qbus device on Unibus system\n");
        dptr->flags |= DEV_DIS;
        return SCPE_ARG;
        }
    return SCPE_OK;
    }

if ((!UNIBUS) && (dptr == &dup_dev)) {
    if (!(dptr->flags & DEV_DIS)) {
        printf ("Can't enable Unibus device on Qbus system\n");
        dptr->flags |= DEV_DIS;
        return SCPE_ARG;
        }
    return SCPE_OK;
    }

if (dup_ldsc == NULL) {                                 /* First time startup */
    dup_desc.ldsc = dup_ldsc = calloc (dup_desc.lines, sizeof(*dup_ldsc));
    for (i = 0; i < dup_desc.lines; i++)                    /* init each line */
        dup_units[i] = dup_unit_template;
    dup_units[dup_desc.lines] = dup_poll_unit_template;
    }
for (i = 0; i < dup_desc.lines; i++)                    /* init each line */
    dup_clear (i, TRUE);
dup_rxi = dup_txi = 0;                                  /* clr master int */
CLR_INT (DUPRX);
CLR_INT (DUPTX);
tmxr_set_modem_control_passthru (&dup_desc);            /* We always want Modem Control */
dup_desc.notelnet = TRUE;                               /* We always want raw tcp socket */
dup_desc.dptr = DUPDPTR;                                /* Connect appropriate device */
dup_desc.uptr = dup_units+dup_desc.lines;               /* Identify polling unit */
sim_cancel (dup_units+dup_desc.lines);                  /* stop poll */
ndev = ((dptr->flags & DEV_DIS)? 0: dup_desc.lines );
if (ndev)
    sim_activate_after (dup_units+dup_desc.lines, DUP_CONNECT_POLL*1000000);
return auto_config (dptr->name, ndev);                  /* auto config */
}

t_stat dup_attach (UNIT *uptr, char *cptr)
{
t_stat r;
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);
char attach_string[512];

if (!cptr || !*cptr)
    return SCPE_ARG;
sprintf (attach_string, "Line=%d,Buffered=16384,%s", dup, cptr);
r = tmxr_open_master (&dup_desc, attach_string);                 /* open master socket */
free (uptr->filename);
uptr->filename = tmxr_line_attach_string(&dup_desc.ldsc[dup]);
if (r != SCPE_OK)                                       /* error? */
    return r;
uptr->flags |= UNIT_ATT;
sim_activate_after (dup_units+dup_desc.lines, 2000000); /* start poll */
return r;
}

t_stat dup_detach (UNIT *uptr)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);
TMLN *lp = &dup_ldsc[dup];

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
uptr->flags &= ~UNIT_ATT;
free (uptr->filename);
uptr->filename = NULL;
free (dup_rcvpacket[dup]);
dup_rcvpacket[dup] = NULL;
dup_rcvpksize[dup] = 0;
dup_rcvpkoffset[dup] = 0;
free (dup_xmtpacket[dup]);
dup_xmtpacket[dup] = NULL;
dup_xmtpksize[dup] = 0;
dup_xmtpkoffset[dup] = 0;
dup_xmtpkrdy[dup] = FALSE;
dup_xmtpkoutoff[dup] = 0;
return tmxr_detach_ln (lp);
}

/* SET/SHOW SPEED processor */

t_stat dup_showspeed (FILE* st, UNIT* uptr, int32 val, void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);

if (dup_speed[dup])
    fprintf(st, "speed=%d bits/sec", dup_speed[dup]);
else
    fprintf(st, "speed=0 (unrestricted)");
return SCPE_OK;
}

t_stat dup_setspeed (UNIT* uptr, int32 val, char* cptr, void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);
t_stat r;
int32 newspeed;

if (cptr == NULL)
    return SCPE_ARG;
newspeed = (int32) get_uint (cptr, 10, 100000000, &r);
if (r != SCPE_OK)
    return r;
dup_speed[dup] = newspeed;
return SCPE_OK;
}

/* SET LINES processor */

t_stat dup_setnl (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 newln, l;
uint32 i;
t_stat r;
DEVICE *dptr = DUPDPTR;

for (i=0; i<dptr->numunits; i++)
    if (dptr->units[i].flags&UNIT_ATT)
        return SCPE_ALATT;
if (cptr == NULL)
    return SCPE_ARG;
newln = (int32) get_uint (cptr, 10, DUP_LINES, &r);
if ((r != SCPE_OK) || (newln == dup_desc.lines))
    return r;
if (newln == 0)
    return SCPE_ARG;
sim_cancel (dup_units + dup_desc.lines);
dup_dib.lnt = newln * IOLN_DUP;                         /* set length */
dup_desc.ldsc = dup_ldsc = realloc(dup_ldsc, newln*sizeof(*dup_ldsc));
for (l=dup_desc.lines; l < newln; l++) {
    memset (&dup_ldsc[l], 0, sizeof(*dup_ldsc));
    dup_units[l] = dup_unit_template;
    }
dup_units[newln] = dup_poll_unit_template;
dup_desc.lines = newln;
dptr->numunits = newln + 1;
return dup_reset (dptr);                            /* setup lines and auto config */
}

t_stat dup_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "Bit Serial Synchronous interface (%s)\n\n", dptr->name);
fprintf (st, "The %s connects two systems to provide a network connection.\n", dptr->name);
fprintf (st, "A maximum of %d %s devices/lines can be configured in the system.\n", DUP_LINES, dptr->name);
fprintf (st, "The number of configured devices can be changed with:\n\n");
fprintf (st, "   sim> SET %s LINES=n\n\n", dptr->name);
fprintf (st, "If you want to experience the actual data rates of the physical hardware you\n");
fprintf (st, "can set the bit rate of the simulated line can be set using the following\n");
fprintf (st, "command:\n\n");
fprintf (st, "   sim> SET %sn SPEED=bps\n\n", dptr->name);
fprintf (st, "Where bps is the number of data bits per second that the simulated line runs\n");
fprintf (st, "at.  Use a value of zero to run at full speed with no artificial\n");
fprintf (st, "throttling.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

t_stat dup_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "The communication line performs input and output through a TCP session\n");
fprintf (st, "connected to a user-specified port.  The ATTACH command specifies the\n");
fprintf (st, "port to be used as well as the peer address:\n\n");
fprintf (st, "   sim> ATTACH %sn {interface:}port,Connect=peerhost:port\n\n", dptr->name);
fprintf (st, "where port is a decimal number between 1 and 65535 that is not being used for\n");
fprintf (st, "other TCP/IP activities.\n\n");
fprintf (st, "Specifying symmetric attach configuration (with both a listen port and\n");
fprintf (st, "a peer address) will cause the side receiving an incoming\n");
fprintf (st, "connection to validate that the connection actually comes from the\n");
fprintf (st, "connecction destination system.\n\n");
return SCPE_OK;
}

char *dup_description (DEVICE *dptr)
{
return (UNIBUS) ? "DUP11 bit synchronous interface" :
                  "DPV11 bit synchronous interface";
}

/* crc16 polynomial x^16 + x^15 + x^2 + 1 (0xA001) CCITT LSB */
static uint16 crc16_nibble[16] = {
    0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
    0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
    };

static uint16 dup_crc16(uint16 crc, const void* vbuf, size_t len)
{
const unsigned char* buf = (const unsigned char*)vbuf;

while(0 != len--) {
    crc = (crc>>4) ^ crc16_nibble[(*buf ^ crc) & 0xF];
    crc = (crc>>4) ^ crc16_nibble[((*buf++)>>4 ^ crc) & 0xF];
    };
return(crc);
}
