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
#include "pdp11_ddcmp.h"
#include "pdp11_dup.h"

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

static uint16 dup_rxcsr[DUP_LINES];
static uint16 dup_rxdbuf[DUP_LINES];
static uint16 dup_parcsr[DUP_LINES];
static uint16 dup_txcsr[DUP_LINES];
static uint16 dup_txdbuf[DUP_LINES];
static t_bool dup_W3[DUP_LINES];
static t_bool dup_W5[DUP_LINES];
static t_bool dup_W6[DUP_LINES];
static uint32 dup_rxi = 0;                                     /* rcv interrupts */
static uint32 dup_txi = 0;                                     /* xmt interrupts */
static uint32 dup_wait[DUP_LINES];                             /* rcv/xmt byte delay */
static uint32 dup_speed[DUP_LINES];                            /* line speed (bits/sec) */
static uint8 *dup_rcvpacket[DUP_LINES];                        /* rcv buffer */
static uint16 dup_rcvpksize[DUP_LINES];                        /* rcv buffer size */
static uint16 dup_rcvpkbytes[DUP_LINES];                       /* rcv buffer size of packet */
static uint16 dup_rcvpkinoff[DUP_LINES];                       /* rcv packet in offset */
static uint8 *dup_xmtpacket[DUP_LINES];                        /* xmt buffer */
static uint16 dup_xmtpksize[DUP_LINES];                        /* xmt buffer size */
static uint16 dup_xmtpkoffset[DUP_LINES];                      /* xmt buffer offset */
static uint32 dup_xmtpkstart[DUP_LINES];                       /* xmt packet start time */
static uint16 dup_xmtpkbytes[DUP_LINES];                       /* xmt packet size of packet */
static uint16 dup_xmtpkdelaying[DUP_LINES];                    /* xmt packet speed delaying completion */
static int32 dup_corruption[DUP_LINES];                       /* data corrupting troll hunger value */

static PACKET_DATA_AVAILABLE_CALLBACK dup_rcv_packet_data_callback[DUP_LINES];
static PACKET_TRANSMIT_COMPLETE_CALLBACK dup_xmt_complete_callback[DUP_LINES];
static MODEM_CHANGE_CALLBACK dup_modem_change_callback[DUP_LINES];

static t_stat dup_rd (int32 *data, int32 PA, int32 access);
static t_stat dup_wr (int32 data, int32 PA, int32 access);
static t_stat dup_set_modem (int32 dup, int32 rxcsr_bits);
static t_stat dup_get_modem (int32 dup);
static t_stat dup_svc (UNIT *uptr);
static t_stat dup_poll_svc (UNIT *uptr);
static t_stat dup_rcv_byte (int32 dup);
static t_stat dup_reset (DEVICE *dptr);
static t_stat dup_attach (UNIT *uptr, CONST char *ptr);
static t_stat dup_detach (UNIT *uptr);
static t_stat dup_clear (int32 dup, t_bool flag);
static int32 dup_rxinta (void);
static int32 dup_txinta (void);
static void dup_update_rcvi (void);
static void dup_update_xmti (void);
static void dup_clr_rxint (int32 dup);
static void dup_set_rxint (int32 dup);
static void dup_clr_txint (int32 dup);
static void dup_set_txint (int32 dup);
static t_stat dup_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat dup_setspeed (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
static t_stat dup_showspeed (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
static t_stat dup_setcorrupt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat dup_showcorrupt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat dup_set_W3 (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
static t_stat dup_show_W3 (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
static t_stat dup_set_W5 (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
static t_stat dup_show_W5 (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
static t_stat dup_set_W6 (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
static t_stat dup_show_W6 (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
static t_stat dup_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static t_stat dup_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char *dup_description (DEVICE *dptr);

/* RXCSR - 16XXX0 - receiver control/status register */

static BITFIELD dup_rxcsr_bits[] = {
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
#define RXCSR_A_MODEM_BITS (RXCSR_M_RING | RXCSR_M_CTS)
#define RXCSR_B_MODEM_BITS (RXCSR_M_DSR | RXCSR_M_DCD)
#define RXCSR_WRITEABLE (RXCSR_M_STRSYN|RXCSR_M_RXIE|RXCSR_M_DSCIE|RXCSR_M_RCVEN|RXCSR_M_SECXMT|RXCSR_M_RTS|RXCSR_M_DTR)

/* RXDBUF - 16XXX2 - receiver Data Buffer register */

static BITFIELD dup_rxdbuf_bits[] = {
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

static BITFIELD dup_parcsr_bits[] = {
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

static BITFIELD dup_txcsr_bits[] = {
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

static BITFIELD dup_txdbuf_bits[] = {
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
    { &dup_rxinta, &dup_txinta },/* int. ack. routines */
    IOLN_DUP,    /* IO space per line */
};

static UNIT dup_unit_template = {
    UDATA (&dup_svc, UNIT_ATTABLE|UNIT_IDLE, 0), 
    };

static UNIT dup_poll_unit_template = {
    UDATA (&dup_poll_svc, UNIT_DIS|UNIT_IDLE, 0), 
    };

static UNIT dup_units[DUP_LINES+1];    /* One unit per line and a polling unit */

static REG dup_reg[] = {
    { BRDATADF (RXCSR,          dup_rxcsr,  DEV_RDX, 16, DUP_LINES, "receive control/status register",  dup_rxcsr_bits) },
    { BRDATADF (RXDBUF,        dup_rxdbuf,  DEV_RDX, 16, DUP_LINES, "receive data buffer",              dup_rxdbuf_bits) },
    { BRDATADF (PARCSR,        dup_parcsr,  DEV_RDX, 16, DUP_LINES, "receive control/status register",  dup_parcsr_bits) },
    { BRDATADF (TXCSR,          dup_txcsr,  DEV_RDX, 16, DUP_LINES, "transmit control/status register", dup_txcsr_bits) },
    { BRDATADF (TXDBUF,        dup_txdbuf,  DEV_RDX, 16, DUP_LINES, "transmit data buffer",             dup_txdbuf_bits) },
    { BRDATAD  (W3,                dup_W3,  DEV_RDX,  1, DUP_LINES, "Clear Option Enable") },
    { BRDATAD  (W5,                dup_W5,  DEV_RDX,  1, DUP_LINES, "A Dataset Control Enable") },
    { BRDATAD  (W6,                dup_W6,  DEV_RDX,  1, DUP_LINES, "A and B Dataset Control Enable") },
    { GRDATAD  (RXINT,            dup_rxi,  DEV_RDX, DUP_LINES,  0, "receive interrupts") },
    { GRDATAD  (TXINT,            dup_txi,  DEV_RDX, DUP_LINES,  0, "transmit interrupts") },
    { BRDATAD  (WAIT,            dup_wait,       10, 32, DUP_LINES, "delay time for transmit/receive bytes"), PV_RSPC },
    { BRDATAD  (SPEED,          dup_speed,       10, 32, DUP_LINES, "line bit rate"), PV_RCOMMA },
    { BRDATAD  (TPOFFSET, dup_xmtpkoffset,  DEV_RDX, 16, DUP_LINES, "transmit assembly packet offset") },
    { BRDATAD  (TPSIZE,    dup_xmtpkbytes,  DEV_RDX, 16, DUP_LINES, "transmit digest packet size") },
    { BRDATAD  (TPDELAY,dup_xmtpkdelaying,  DEV_RDX, 16, DUP_LINES, "transmit packet completion delay") },
    { BRDATAD  (TPSTART,   dup_xmtpkstart,  DEV_RDX, 32, DUP_LINES, "transmit digest packet start time") },
    { BRDATAD  (RPINOFF,   dup_rcvpkinoff,  DEV_RDX, 16, DUP_LINES, "receive digest packet offset") },
    { BRDATAD  (CORRUPT,   dup_corruption,  DEV_RDX, 32, DUP_LINES, "data corruption factor (0.1%)") },
    { NULL }
    };

static TMLN *dup_ldsc = NULL;                                  /* line descriptors */
static TMXR dup_desc = { INITIAL_DUP_LINES, 0, 0, NULL };      /* mux descriptor */

static MTAB dup_mod[] = {
    { MTAB_XTD|MTAB_VUN,          0, "SPEED", "SPEED=bits/sec (0=unrestricted)" ,
        &dup_setspeed, &dup_showspeed, NULL, "Display rate limit" },
    { MTAB_XTD|MTAB_VUN,          0, "CORRUPTION", "CORRUPTION=factor (0=uncorrupted)" ,
        &dup_setcorrupt, &dup_showcorrupt, NULL, "Display corruption factor (0.1% of packets)" },
    { MTAB_XTD|MTAB_VUN,          1, "W3", NULL ,
        NULL, &dup_show_W3, NULL, "Display Reset Option" },
    { MTAB_XTD|MTAB_VUN,          1, NULL, "W3" ,
        &dup_set_W3, NULL,         NULL, "Enable Reset Option" },
    { MTAB_XTD|MTAB_VUN,          0, NULL, "NOW3" ,
        &dup_set_W3, NULL,         NULL, "Disable Reset Option" },
    { MTAB_XTD|MTAB_VUN,          1, "W5", NULL ,
        NULL, &dup_show_W5, NULL, "Display A Dataset Control Option" },
    { MTAB_XTD|MTAB_VUN,          1, NULL, "W5" ,
        &dup_set_W5, NULL,         NULL, "Enable A Dataset Control Option" },
    { MTAB_XTD|MTAB_VUN,          0, NULL, "NOW5" ,
        &dup_set_W5, NULL,         NULL, "Disable A Dataset Control Option" },
    { MTAB_XTD|MTAB_VUN,          1, "W6", NULL ,
        NULL, &dup_show_W6, NULL, "Display A & B Dataset Control Option" },
    { MTAB_XTD|MTAB_VUN,          1, NULL, "W6" ,
        &dup_set_W6, NULL,         NULL, "Enable A & B Dataset Control Option" },
    { MTAB_XTD|MTAB_VUN,          0, NULL, "NOW6" ,
        &dup_set_W6, NULL,         NULL, "Disable A & B Dataset Control  Option" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, "VECTOR", "VECTOR",
        &set_vec, &show_vec_mux, (void *) &dup_desc, "Interrupt vector" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dup_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dup_setnl, &tmxr_show_lines, (void *) &dup_desc, "Display number of lines" },
    { 0 }
    };

/* debugging bitmaps */
#define DBG_REG  0x0001                                 /* trace read/write registers */
#define DBG_INT  0x0002                                 /* display transfer requests */
#define DBG_PKT  (TMXR_DBG_PXMT|TMXR_DBG_PRCV)          /* display packets */
#define DBG_XMT  TMXR_DBG_XMT                           /* display Transmitted Data */
#define DBG_RCV  TMXR_DBG_RCV                           /* display Received Data */
#define DBG_MDM  TMXR_DBG_MDM                           /* display Modem SignalTransitions */
#define DBG_CON  TMXR_DBG_CON                           /* display connection activities */
#define DBG_TRC  TMXR_DBG_TRC                           /* display trace routine calls */
#define DBG_ASY  TMXR_DBG_ASY                           /* display Asynchronous Activities */

static DEBTAB dup_debug[] = {
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
    &dup_dib, DEV_DIS | DEV_DISABLE | DEV_UBUS | DEV_DEBUG | DEV_DONTAUTO, 0,
    dup_debug, NULL, NULL, &dup_help, dup_help_attach, &dup_desc, 
    &dup_description
    };

DEVICE dpv_dev = {
    "DPV", dup_units, dup_reg, dup_mod,
    2, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &dup_reset,
    NULL, &dup_attach, &dup_detach,
    &dup_dib, DEV_DIS | DEV_DISABLE | DEV_QBUS | DEV_DEBUG | DEV_DONTAUTO, 0,
    dup_debug, NULL, NULL, &dup_help, dup_help_attach, &dup_desc, 
    &dup_description
    };

#define DUPDPTR ((UNIBUS) ? &dup_dev : &dpv_dev)

/* Register names for Debug tracing */
static const char *dup_rd_regs[] =
    {"RXCSR ", "RXDBUF", "TXCSR ", "TXDBUF" };
static const char *dup_wr_regs[] = 
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

static t_stat dup_rd (int32 *data, int32 PA, int32 access)
{
static BITFIELD* bitdefs[] = {dup_rxcsr_bits, dup_rxdbuf_bits, dup_txcsr_bits, dup_txdbuf_bits};
static uint16 *regs[] = {dup_rxcsr, dup_rxdbuf, dup_txcsr, dup_txdbuf};
int32 dup = ((PA - dup_dib.ba) >> 3);                   /* get line num */
int32 orig_val;

if (dup >= dup_desc.lines)                              /* validate line number */
    return SCPE_IERR;

orig_val = regs[(PA >> 1) & 03][dup];
switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

    case 00:                                            /* RXCSR */
        dup_get_modem (dup);
        *data = dup_rxcsr[dup];
        dup_rxcsr[dup] &= ~(RXCSR_M_DSCHNG|RXCSR_M_BDATSET);
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

static t_stat dup_wr (int32 data, int32 PA, int32 access)
{
static BITFIELD* bitdefs[] = {dup_rxcsr_bits, dup_parcsr_bits, dup_txcsr_bits, dup_txdbuf_bits};
static uint16 *regs[] = {dup_rxcsr, dup_parcsr, dup_txcsr, dup_txdbuf};
int32 dup = ((PA - dup_dib.ba) >> 3);                   /* get line num */
int32 orig_val;

if (dup >= dup_desc.lines)                              /* validate line number */
    return SCPE_IERR;

orig_val = regs[(PA >> 1) & 03][dup];
if (PA & 1)                                             /* unaligned byte access? */
    data = ((data << 8) | (orig_val & 0xFF)) & 0xFFFF;  /* Merge with original word */
else
    if (access == WRITEB)                               /* byte access? */
        data = (orig_val & 0xFF00) | (data & 0xFF);     /* Merge with original high word */

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
            dup_rxdbuf[dup] &= ~RXDBUF_M_RXDBUF;
            dup_rxcsr[dup] &= ~RXCSR_M_RXACT;
            if ((dup_rcvpkinoff[dup] != 0) || 
                (dup_rcvpkbytes[dup] != 0))
                dup_rcvpkinoff[dup] = dup_rcvpkbytes[dup] = 0;
            }
        if ((!(dup_rxcsr[dup] & RXCSR_M_RXIE)) && 
            (orig_val & RXCSR_M_RXIE))                  /* Downward transition of receiver interrupt enable */
            dup_clr_rxint (dup);
        if ((dup_rxcsr[dup] & RXCSR_M_RXIE) && (dup_rxcsr[dup] & RXCSR_M_RXDONE))
            dup_set_rxint (dup);
        break;

    case 01:                                            /* PARCSR */
        dup_parcsr[dup] &= ~PARCSR_WRITEABLE;
        dup_parcsr[dup] |= (data & PARCSR_WRITEABLE);
        break;

    case 02:                                            /* TXCSR */
        dup_txcsr[dup] &= ~TXCSR_WRITEABLE;
        dup_txcsr[dup] |= (data & TXCSR_WRITEABLE);
        if (dup_txcsr[dup] & TXCSR_M_DRESET) {
            dup_clear(dup, dup_W3[dup]);
            break;
            }
        if (TXCSR_GETMAISEL(dup_txcsr[dup]) != TXCSR_GETMAISEL(orig_val)) { /* Maint Select Changed */
            switch (TXCSR_GETMAISEL(dup_txcsr[dup])) {
                case 0:  /* User/Normal Mode */
                    tmxr_set_line_loopback (&dup_desc.ldsc[dup], FALSE);
                    break;
                case 1:  /* External Loopback Mode */
                case 2:  /* Internal Loopback Mode */
                    tmxr_set_line_loopback (&dup_desc.ldsc[dup], TRUE);
                    break;
                case 3:  /* System Test Mode */
                    break;
                }
            }
        if ((dup_txcsr[dup] & TXCSR_M_TXACT) && 
            (!(orig_val & TXCSR_M_TXACT))    && 
            (orig_val & TXCSR_M_TXDONE)) {
            dup_txcsr[dup] &= ~TXCSR_M_TXDONE;
            }
        if ((!(dup_txcsr[dup] & TXCSR_M_SEND)) && 
            (orig_val & TXCSR_M_SEND)) {
            dup_txcsr[dup] &= ~TXCSR_M_TXACT;
            dup_put_msg_bytes (dup, NULL, 0, FALSE, TRUE);
            }
        if ((dup_txcsr[dup] & TXCSR_M_HALFDUP) ^ (orig_val & TXCSR_M_HALFDUP))
            tmxr_set_line_halfduplex (dup_desc.ldsc+dup, dup_txcsr[dup] & TXCSR_M_HALFDUP);
        if ((dup_txcsr[dup] & TXCSR_M_TXIE) && 
            (!(orig_val & TXCSR_M_TXIE))    && 
            (dup_txcsr[dup] & TXCSR_M_TXDONE)) {
            dup_set_txint (dup);
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
dup_get_modem (dup);
return SCPE_OK;
}

static t_stat dup_set_modem (int32 dup, int32 rxcsr_bits)
{
int32 bits_to_set, bits_to_clear;

if ((rxcsr_bits & (RXCSR_M_DTR | RXCSR_M_RTS)) == (dup_rxcsr[dup] & (RXCSR_M_DTR | RXCSR_M_RTS)))
    return SCPE_OK;
bits_to_set = ((rxcsr_bits & RXCSR_M_DTR) ? TMXR_MDM_DTR : 0) | ((rxcsr_bits & RXCSR_M_RTS) ? TMXR_MDM_RTS : 0);
bits_to_clear = (~bits_to_set) & (TMXR_MDM_DTR | TMXR_MDM_RTS);
tmxr_set_get_modem_bits (dup_desc.ldsc+dup, bits_to_set, bits_to_clear, NULL);
return SCPE_OK;
}

static t_stat dup_get_modem (int32 dup)
{
int32 modem_bits;
uint16 old_rxcsr = dup_rxcsr[dup];
int32 old_rxcsr_a_modem_bits, new_rxcsr_a_modem_bits, old_rxcsr_b_modem_bits, new_rxcsr_b_modem_bits;
TMLN *lp = &dup_desc.ldsc[dup];
t_bool new_modem_change = FALSE;

if (dup_W5[dup])
    old_rxcsr_a_modem_bits = dup_rxcsr[dup] & (RXCSR_M_RING | RXCSR_M_CTS | RXCSR_M_DSR | RXCSR_M_DCD);
else
    old_rxcsr_a_modem_bits = dup_rxcsr[dup] & (RXCSR_M_RING | RXCSR_M_CTS);
if (dup_W6[dup])
    old_rxcsr_b_modem_bits = dup_rxcsr[dup] & RXCSR_B_MODEM_BITS;
else
    old_rxcsr_b_modem_bits = 0;
old_rxcsr_a_modem_bits = dup_rxcsr[dup] & RXCSR_A_MODEM_BITS;
old_rxcsr_b_modem_bits = dup_rxcsr[dup] & RXCSR_B_MODEM_BITS;
tmxr_set_get_modem_bits (lp, 0, 0, &modem_bits);
if (dup_W5[dup])
    new_rxcsr_a_modem_bits = (((modem_bits & TMXR_MDM_RNG) ? RXCSR_M_RING : 0) |
                              ((modem_bits & TMXR_MDM_CTS) ? RXCSR_M_CTS : 0) |
                              ((modem_bits & TMXR_MDM_DSR) ? RXCSR_M_DSR : 0) |
                              ((modem_bits & TMXR_MDM_DCD) ? RXCSR_M_DCD : 0));
else
    new_rxcsr_a_modem_bits = (((modem_bits & TMXR_MDM_RNG) ? RXCSR_M_RING : 0) |
                              ((modem_bits & TMXR_MDM_CTS) ? RXCSR_M_CTS : 0));
if (dup_W6[dup])
    new_rxcsr_b_modem_bits = (((modem_bits & TMXR_MDM_DSR) ? RXCSR_M_DSR : 0) |
                              ((modem_bits & TMXR_MDM_DCD) ? RXCSR_M_DCD : 0));
else
    new_rxcsr_b_modem_bits = 0;
dup_rxcsr[dup] &= ~(RXCSR_A_MODEM_BITS | RXCSR_B_MODEM_BITS);
dup_rxcsr[dup] |= new_rxcsr_a_modem_bits | new_rxcsr_b_modem_bits;
if (old_rxcsr_a_modem_bits != new_rxcsr_a_modem_bits) {
    dup_rxcsr[dup] |= RXCSR_M_DSCHNG;
    new_modem_change = TRUE;
    }
if (old_rxcsr_b_modem_bits != new_rxcsr_b_modem_bits) {
    dup_rxcsr[dup] |= RXCSR_M_BDATSET;
    new_modem_change = TRUE;
    }
if (new_modem_change) {
    sim_debug(DBG_MDM, DUPDPTR, "dup_get_modem() - Modem Signal Change ");
    sim_debug_bits(DBG_MDM, DUPDPTR, dup_rxcsr_bits, (uint32)old_rxcsr, (uint32)dup_rxcsr[dup], TRUE);
    }
if (dup_modem_change_callback[dup] && new_modem_change)
     dup_modem_change_callback[dup](dup);
if ((dup_rxcsr[dup] & RXCSR_M_DSCHNG) &&
    ((dup_rxcsr[dup] & RXCSR_M_DSCHNG) != (old_rxcsr & RXCSR_M_DSCHNG)) &&
    (dup_rxcsr[dup] & RXCSR_M_DSCIE))
    dup_set_rxint (dup);
return SCPE_OK;
}

/*
 * Public routines for use by other devices (i.e. KDP11)
 */

int32 dup_csr_to_linenum (int32 CSRPA)
{
DEVICE *dptr = DUPDPTR;
DIB *dib = (DIB *)dptr->ctxt;

CSRPA += IOPAGEBASE;
if ((dib->ba > (uint32)CSRPA) || ((uint32)CSRPA > (dib->ba + dib->lnt)) || (DUPDPTR->flags & DEV_DIS))
    return -1;

return ((uint32)CSRPA - dib->ba)/IOLN_DUP;
}

void dup_set_callback_mode (int32 dup, PACKET_DATA_AVAILABLE_CALLBACK receive, PACKET_TRANSMIT_COMPLETE_CALLBACK transmit, MODEM_CHANGE_CALLBACK modem)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return;
dup_rcv_packet_data_callback[dup] = receive;
dup_xmt_complete_callback[dup] = transmit;
dup_modem_change_callback[dup] = modem;
}

int32 dup_get_DCD (int32 dup)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return -1;
return (dup_rxcsr[dup] & RXCSR_M_DCD) ? 1 : 0;
}

int32 dup_get_DSR (int32 dup)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return -1;
return (dup_rxcsr[dup] & RXCSR_M_DSR) ? 1 : 0;
}

int32 dup_get_CTS (int32 dup)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return -1;
return (dup_rxcsr[dup] & RXCSR_M_CTS) ? 1 : 0;
}

int32 dup_get_RING (int32 dup)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return -1;
return (dup_rxcsr[dup] & RXCSR_M_RING) ? 1 : 0;
}

int32 dup_get_RCVEN (int32 dup)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return -1;
return (dup_rxcsr[dup] & RXCSR_M_RCVEN) ? 1 : 0;
}

t_stat dup_set_DTR (int32 dup, t_bool state)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return SCPE_IERR;
dup_set_modem (dup, (state ? RXCSR_M_DTR : 0) | (dup_rxcsr[dup] & RXCSR_M_RTS));
if (state)
    dup_rxcsr[dup] |= RXCSR_M_DTR;
else
    dup_rxcsr[dup] &= ~RXCSR_M_DTR;
dup_ldsc[dup].rcve = state;
dup_get_modem (dup);
return SCPE_OK;
}

t_stat dup_set_RTS (int32 dup, t_bool state)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return SCPE_IERR;
dup_set_modem (dup, (state ? RXCSR_M_RTS : 0) | (dup_rxcsr[dup] & RXCSR_M_DTR));
if (state)
    dup_rxcsr[dup] |= RXCSR_M_RTS;
else
    dup_rxcsr[dup] &= ~RXCSR_M_RTS;
dup_get_modem (dup);
return SCPE_OK;
}

t_stat dup_set_RCVEN (int32 dup, t_bool state)
{
uint16 orig_val;

if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return SCPE_IERR;
orig_val = dup_rxcsr[dup];
dup_rxcsr[dup] &= ~RXCSR_M_RCVEN;
dup_rxcsr[dup] |= (state ? RXCSR_M_RCVEN : 0);
if ((dup_rxcsr[dup] & RXCSR_M_RCVEN) && 
    (!(orig_val & RXCSR_M_RCVEN))) {            /* Upward transition of receiver enable */
    UNIT *uptr = dup_units + dup;

    dup_poll_svc (uptr);                        /* start any pending receive */
    }
return SCPE_OK;
}

t_stat dup_setup_dup (int32 dup, t_bool enable, t_bool protocol_DDCMP, t_bool crc_inhibit, t_bool halfduplex, uint8 station)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return SCPE_IERR;
if (!enable) {
    dup_clear(dup, TRUE);
    return SCPE_OK;
    }
if (!protocol_DDCMP) {
    return SCPE_NOFNC;              /* only DDCMP for now */
    }
if (crc_inhibit) {
    return SCPE_ARG;                /* Must enable CRC for DDCMP */
    }
/* These settings reflect how RSX operates a bare DUP when used for 
   DECnet communications */
dup_clear(dup, FALSE);
dup_rxcsr[dup] |= RXCSR_M_STRSYN | RXCSR_M_RCVEN;
dup_parcsr[dup] = PARCSR_M_DECMODE | (DDCMP_SYN << PARCSR_V_ADSYNC);
dup_txcsr[dup] &= TXCSR_M_HALFDUP;
dup_txcsr[dup] |= (halfduplex ? TXCSR_M_HALFDUP : 0);
tmxr_set_line_halfduplex (dup_desc.ldsc+dup, dup_txcsr[dup] & TXCSR_M_HALFDUP);
return dup_set_DTR (dup, TRUE);
}

t_stat dup_reset_dup (int32 dup)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return SCPE_IERR;
dup_clear(dup, dup_W3[dup]);
return SCPE_OK;
}

t_stat dup_set_W3_option (int32 dup, t_bool state)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return SCPE_IERR;
dup_W3[dup] = state;
return SCPE_OK;
}

t_stat dup_set_W5_option (int32 dup, t_bool state)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return SCPE_IERR;
dup_W5[dup] = state;
return SCPE_OK;
}

t_stat dup_set_W6_option (int32 dup, t_bool state)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return SCPE_IERR;
dup_W6[dup] = state;
return SCPE_OK;
}


t_bool dup_put_msg_bytes (int32 dup, uint8 *bytes, size_t len, t_bool start, t_bool end)
{
t_bool breturn = FALSE;

if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return FALSE;

if (!tmxr_tpbusyln(&dup_ldsc[dup])) {  /* Not Busy sending? */
    if (start) {
        dup_xmtpkoffset[dup] = 0;
        dup_xmtpkdelaying[dup] = 0;
        dup_xmtpkstart[dup] = sim_grtime();
        }
    if (dup_xmtpkoffset[dup] + 2 + len > dup_xmtpksize[dup]) {
        dup_xmtpksize[dup] += 2 + (uint16)len;
        dup_xmtpacket[dup] = (uint8 *)realloc (dup_xmtpacket[dup], dup_xmtpksize[dup]);
        }
    /* Strip sync bytes at the beginning of a message */
    while (len && (dup_xmtpkoffset[dup] == 0) && (bytes[0] == DDCMP_SYN)) {
        --len;
        ++bytes;
        }
    /* Insert remaining bytes into transmit buffer */
    if (len) {
        memcpy (&dup_xmtpacket[dup][dup_xmtpkoffset[dup]], bytes, len);
        dup_xmtpkoffset[dup] += (uint16)len;
        }
    dup_txcsr[dup] |= TXCSR_M_TXDONE;
    if (dup_txcsr[dup] & TXCSR_M_TXIE)
        dup_set_txint (dup);
    /* On End of Message, insert CRC and flag delivery start */
    if (end) {
        uint16 crc16 = ddcmp_crc16 (0, dup_xmtpacket[dup], dup_xmtpkoffset[dup]);

        dup_xmtpacket[dup][dup_xmtpkoffset[dup]++] = crc16 & 0xFF;
        dup_xmtpacket[dup][dup_xmtpkoffset[dup]++] = crc16 >> 8;
        if ((dup_xmtpkoffset[dup] > 8) || (dup_xmtpacket[dup][0] == DDCMP_ENQ)) {
            dup_xmtpkbytes[dup] = dup_xmtpkoffset[dup];
            ddcmp_tmxr_put_packet_ln (&dup_ldsc[dup], dup_xmtpacket[dup], dup_xmtpkbytes[dup], dup_corruption[dup]);
            }
        }
    breturn = TRUE;
    }
sim_debug (DBG_TRC, DUPDPTR, "dup_put_msg_bytes(dup=%d, len=%d, start=%s, end=%s) %s\n", 
           dup, (int)len, start ? "TRUE" : "FALSE", end ? "TRUE" : "FALSE", breturn ? "Good" : "Busy");
if (breturn && (tmxr_tpbusyln (&dup_ldsc[dup]) || dup_xmtpkbytes[dup])) {
    if (dup_xmt_complete_callback[dup])
        dup_svc(dup_units+dup);
    }
return breturn;
}

t_stat dup_get_packet (int32 dup, const uint8 **pbuf, uint16 *psize)
{
if ((dup < 0) || (dup >= dup_desc.lines) || (DUPDPTR->flags & DEV_DIS))
    return SCPE_IERR;

if (*pbuf == &dup_rcvpacket[dup][0]) {
    *pbuf = NULL;
    *psize = 0;
    dup_rcvpkinoff[dup] = dup_rcvpkbytes[dup] = 0;
    dup_rxcsr[dup] &= ~RXCSR_M_RXACT;
    }
if ((dup_rcvpkinoff[dup] == 0) && (dup_rcvpkbytes[dup] != 0)) {
    *pbuf = &dup_rcvpacket[dup][0];
    *psize = dup_rcvpkbytes[dup];
    }
sim_debug (DBG_TRC, DUPDPTR, "dup_get_packet(dup=%d, psize=%d)\n", 
           dup, (int)*psize);
return SCPE_OK;
}

static t_stat dup_rcv_byte (int32 dup)
{
sim_debug (DBG_TRC, DUPDPTR, "dup_rcv_byte(dup=%d) - %s, byte %d of %d\n", dup, 
           (dup_rxcsr[dup] & RXCSR_M_RCVEN) ? "enabled" : "disabled",
           dup_rcvpkinoff[dup], dup_rcvpkbytes[dup]);
if (!(dup_rxcsr[dup] & RXCSR_M_RCVEN) || (dup_rcvpkbytes[dup] == 0) || (dup_rxcsr[dup] & RXCSR_M_RXDONE))
    return SCPE_OK;
if (dup_rcv_packet_data_callback[dup]) {
    sim_debug (DBG_TRC, DUPDPTR, "dup_rcv_byte(dup=%d, psize=%d) - Invoking Receive Data callback\n", 
               dup, (int)dup_rcvpkbytes[dup]);
    dup_rcv_packet_data_callback[dup](dup, dup_rcvpkbytes[dup]);
    return SCPE_OK;
    }
dup_rxcsr[dup] |= RXCSR_M_RXACT;
dup_rxdbuf[dup] &= ~RXDBUF_M_RCRCER;
dup_rxdbuf[dup] &= ~RXDBUF_M_RXDBUF;
dup_rxdbuf[dup] |= dup_rcvpacket[dup][dup_rcvpkinoff[dup]++];
dup_rxcsr[dup] |= RXCSR_M_RXDONE;
if (((dup_rcvpkinoff[dup] == 8) || 
     (dup_rcvpkinoff[dup] >= dup_rcvpkbytes[dup])) &&
    (0 == ddcmp_crc16 (0, dup_rcvpacket[dup], dup_rcvpkinoff[dup])))
    dup_rxdbuf[dup] |= RXDBUF_M_RCRCER;
else
    dup_rxdbuf[dup] &= ~RXDBUF_M_RCRCER;
if (dup_rcvpkinoff[dup] >= dup_rcvpkbytes[dup]) {
    dup_rcvpkinoff[dup] = dup_rcvpkbytes[dup] = 0;
    dup_rxcsr[dup] &= ~RXCSR_M_RXACT;
    }
if (dup_rxcsr[dup] & RXCSR_M_RXIE)
    dup_set_rxint (dup);
return SCPE_OK;
}

/* service routine to delay device activity */

static t_stat dup_svc (UNIT *uptr)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);
TMLN *lp = &dup_desc.ldsc[dup];

sim_debug(DBG_TRC, DUPDPTR, "dup_svc(dup=%d)\n", dup);
if (!(dup_txcsr[dup] & TXCSR_M_TXDONE) && (!tmxr_tpbusyln (lp))) {
    uint8 data = dup_txdbuf[dup] & TXDBUF_M_TXDBUF;

    dup_put_msg_bytes (dup, &data, (dup_txdbuf[dup] & TXDBUF_M_TEOM) && (dptr == &dup_dev) ? 0 : 1, dup_txdbuf[dup] & TXDBUF_M_TSOM, (dup_txdbuf[dup] & TXDBUF_M_TEOM));
    if (tmxr_tpbusyln (lp)) { /* Packet ready to send? */
        sim_debug(DBG_TRC, DUPDPTR, "dup_svc(dup=%d) - Packet Done %d bytes\n", dup, dup_xmtpkoffset[dup]);
        }
    }
if ((tmxr_tpbusyln (lp) || dup_xmtpkbytes[dup]) && (lp->xmte || (!lp->conn))) {
    int32 start = tmxr_tpbusyln (lp) ? tmxr_tpqln (lp) + tmxr_tqln (lp) : dup_xmtpkbytes[dup];
    int32 remain = tmxr_send_buffered_data (lp);/* send any buffered data */
    if (remain) {
        sim_debug(DBG_PKT, DUPDPTR, "dup_svc(dup=%d) - Packet Transmission Stalled with %d bytes remaining\n", dup, remain);
        }
    else {
        if (!lp->conn) {
            if (dup_xmtpkoffset[dup]) {
                sim_debug(DBG_PKT, DUPDPTR, "dup_svc(dup=%d) - %d byte packet transmission with link down (dropped)\n", dup, dup_xmtpkoffset[dup]);
                }
            dup_get_modem (dup);
            }
        else {
            sim_debug(DBG_PKT, DUPDPTR, "dup_svc(dup=%d) - %d byte packet transmission complete\n", dup, dup_xmtpkbytes[dup]);
            }
        dup_xmtpkoffset[dup] = 0;
        }
    if (!tmxr_tpbusyln (lp)) {               /* Done transmitting? */
        if (((start - remain) > 0) && dup_speed[dup] && dup_xmt_complete_callback[dup] && !dup_xmtpkdelaying[dup]) { /* just done, and speed limited using packet interface? */
            dup_xmtpkdelaying[dup] = 1;
            sim_activate_notbefore (uptr, dup_xmtpkstart[dup] + (uint32)((tmr_poll*clk_tps)*((double)dup_xmtpkbytes[dup]*8)/dup_speed[dup]));
            }
        else {
            dup_txcsr[dup] &= ~TXCSR_M_TXACT;   /* Set idle */
            dup_xmtpkbytes[dup] = 0;
            dup_xmtpkdelaying[dup] = 0;
            if (dup_xmt_complete_callback[dup])
                dup_xmt_complete_callback[dup](dup, (dup_rxcsr[dup] & RXCSR_M_DCD) ? 0 : 1);
            }
        }
    }
if (dup_rxcsr[dup] & RXCSR_M_RXACT)
    dup_rcv_byte (dup);
return SCPE_OK;
}

static t_stat dup_poll_svc (UNIT *uptr)
{
int32 dup, active, attached;

sim_debug(DBG_TRC, DUPDPTR, "dup_poll_svc()\n");

tmxr_poll_conn(&dup_desc);
tmxr_poll_rx (&dup_desc);
tmxr_poll_tx (&dup_desc);
for (dup=active=attached=0; dup < dup_desc.lines; dup++) {
    TMLN *lp = &dup_desc.ldsc[dup];

    if (dup_units[dup].flags & UNIT_ATT)
        ++attached;
    if (dup_ldsc[dup].conn)
        ++active;
    dup_get_modem (dup);
    if (lp->xmte && tmxr_tpbusyln(lp)) {
        sim_debug(DBG_PKT, DUPDPTR, "dup_poll_svc(dup=%d) - Packet Transmission of remaining %d bytes restarting...\n", dup, tmxr_tpqln (lp));
        dup_svc (&dup_units[dup]);              /* Flush pending output */
        }
    if (!(dup_rxcsr[dup] & RXCSR_M_RXACT)) {
        const uint8 *buf;
        uint16 size;

        if (dup_parcsr[dup] & PARCSR_M_DECMODE)
            ddcmp_tmxr_get_packet_ln (lp, &buf, &size, dup_corruption[dup]);
        else {
            size_t size_t_size;

            tmxr_get_packet_ln (lp, &buf, &size_t_size);
            size = (uint16)size_t_size;
            }
        if (buf) {
            if (dup_rcvpksize[dup] < size) {
                dup_rcvpksize[dup] = size;
                dup_rcvpacket[dup] = (uint8 *)realloc (dup_rcvpacket[dup], dup_rcvpksize[dup]);
                }
            memcpy (dup_rcvpacket[dup], buf, size);
            dup_rcvpkbytes[dup] = size;
            dup_rcvpkinoff[dup] = 0;
            dup_rxcsr[dup] |= RXCSR_M_RXACT;
            dup_rcv_byte (dup);
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

/* Interrupt routines */

static void dup_clr_rxint (int32 dup)
{
dup_rxi = dup_rxi & ~(1 << dup);                        /* clr mux rcv int */
if (dup_rxi == 0)                                       /* all clr? */
    CLR_INT (DUPRX);
else SET_INT (DUPRX);                                   /* no, set intr */
return;
}

static void dup_set_rxint (int32 dup)
{
dup_rxi = dup_rxi | (1 << dup);                         /* set mux rcv int */
SET_INT (DUPRX);                                        /* set master intr */
sim_debug(DBG_INT, DUPDPTR, "dup_set_rxint(dup=%d)\n", dup);
return;
}

static int32 dup_rxinta (void)
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

static void dup_clr_txint (int32 dup)
{
dup_txi = dup_txi & ~(1 << dup);                        /* clr mux xmt int */
if (dup_txi == 0)                                       /* all clr? */
    CLR_INT (DUPTX);
else SET_INT (DUPTX);                                   /* no, set intr */
return;
}

static void dup_set_txint (int32 dup)
{
dup_txi = dup_txi | (1 << dup);                         /* set mux xmt int */
SET_INT (DUPTX);                                        /* set master intr */
sim_debug(DBG_INT, DUPDPTR, "dup_set_txint(dup=%d)\n", dup);
return;
}

static int32 dup_txinta (void)
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

static t_stat dup_clear (int32 dup, t_bool flag)
{
sim_debug(DBG_TRC, DUPDPTR, "dup_clear(dup=%d,flag=%d)\n", dup, flag);

dup_rxdbuf[dup] = 0;                                    /* silo empty */
dup_txdbuf[dup] = 0;
dup_parcsr[dup] = 0;                                    /* no params */
dup_txcsr[dup] = TXCSR_M_TXDONE;                        /* clear CSR */
dup_wait[dup] = DUP_WAIT;                               /* initial/default byte delay */
if (flag) {                                             /* INIT? clr all */
    dup_rxcsr[dup] = 0;
    dup_set_modem (dup, dup_rxcsr[dup]);                /* push change out to line */
    }
else
    dup_rxcsr[dup] &= ~(RXCSR_M_DTR|RXCSR_M_RTS);       /* else save dtr & rts */
dup_clr_rxint (dup);                                    /* clear int */
dup_clr_txint (dup);
if (!dup_ldsc[dup].conn)                                /* set xmt enb */
    dup_ldsc[dup].xmte = 1;
dup_ldsc[dup].rcve = 0;                                 /* clr rcv enb */
return SCPE_OK;
}

static t_stat dup_reset (DEVICE *dptr)
{
t_stat r;
int32 i, ndev, attached = 0;

sim_debug(DBG_TRC, dptr, "dup_reset()\n");

dup_desc.packet = TRUE;
dup_desc.buffered = 16384;
if ((UNIBUS) && (dptr == &dpv_dev)) {
    if (!(dptr->flags & DEV_DIS)) {
        sim_printf ("Can't enable Qbus device on Unibus system\n");
        dptr->flags |= DEV_DIS;
        return SCPE_ARG;
        }
    return SCPE_OK;
    }

if ((!UNIBUS) && (dptr == &dup_dev)) {
    if (!(dptr->flags & DEV_DIS)) {
        sim_printf ("Can't enable Unibus device on Qbus system\n");
        dptr->flags |= DEV_DIS;
        return SCPE_ARG;
        }
    return SCPE_OK;
    }

if (dup_ldsc == NULL) {                                 /* First time startup */
    dup_desc.ldsc = dup_ldsc = (TMLN *)calloc (dup_desc.lines, sizeof(*dup_ldsc));
    for (i = 0; i < dup_desc.lines; i++) {              /* init each line */
        dup_units[i] = dup_unit_template;
        if (dup_units[i].flags & UNIT_ATT)
            ++attached;
        }
    dup_units[dup_desc.lines] = dup_poll_unit_template;
    /* Initialize to standard factory Option Jumper Settings */
    for (i = 0; i < DUP_LINES; i++) {
        dup_W3[i] = TRUE;
        dup_W5[i] = FALSE;
        dup_W6[i] = TRUE;
        }
    }
for (i = 0; i < dup_desc.lines; i++) {                  /* init each line */
    dup_clear (i, TRUE);
    if (dup_units[i].flags & UNIT_ATT)
        ++attached;
    }
dup_rxi = dup_txi = 0;                                  /* clr master int */
CLR_INT (DUPRX);
CLR_INT (DUPTX);
tmxr_set_modem_control_passthru (&dup_desc);            /* We always want Modem Control */
dup_desc.notelnet = TRUE;                               /* We always want raw tcp socket */
dup_desc.dptr = DUPDPTR;                                /* Connect appropriate device */
dup_desc.uptr = dup_units+dup_desc.lines;               /* Identify polling unit */
sim_cancel (dup_units+dup_desc.lines);                  /* stop poll */
ndev = ((dptr->flags & DEV_DIS)? 0: dup_desc.lines );
r = auto_config (dptr->name, ndev);                     /* auto config */
if ((r == SCPE_OK) && (attached))
    sim_activate_after (dup_units+dup_desc.lines, DUP_CONNECT_POLL*1000000);/* start poll */
return r;
}

static t_stat dup_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);
char attach_string[512];

if (!cptr || !*cptr)
    return SCPE_ARG;
if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOATT;
sprintf (attach_string, "Line=%d,%s", dup, cptr);
r = tmxr_open_master (&dup_desc, attach_string);                 /* open master socket */
free (uptr->filename);
uptr->filename = tmxr_line_attach_string(&dup_desc.ldsc[dup]);
if (r != SCPE_OK)                                       /* error? */
    return r;
uptr->flags |= UNIT_ATT;
sim_activate_after (dup_units+dup_desc.lines, DUP_CONNECT_POLL*1000000);/* start poll */
return r;
}

static t_stat dup_detach (UNIT *uptr)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);
TMLN *lp = &dup_ldsc[dup];
int32 i, attached;
t_stat r;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
sim_cancel (uptr);
uptr->flags &= ~UNIT_ATT;
for (i=attached=0; i<dup_desc.lines; i++)
    if (dup_dev.units[i].flags & UNIT_ATT)
        ++attached;
if (!attached)
    sim_cancel (dup_units+dup_desc.lines);              /* stop poll on last detach */
r = tmxr_detach_ln (lp);
free (uptr->filename);
uptr->filename = NULL;
free (dup_rcvpacket[dup]);
dup_rcvpacket[dup] = NULL;
dup_rcvpksize[dup] = 0;
dup_rcvpkbytes[dup] = 0;
free (dup_xmtpacket[dup]);
dup_xmtpacket[dup] = NULL;
dup_xmtpksize[dup] = 0;
dup_xmtpkoffset[dup] = 0;
return r;
}

/* SET/SHOW SPEED processor */

static t_stat dup_showspeed (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);

if (dup_speed[dup])
    fprintf(st, "speed=%d bits/sec", dup_speed[dup]);
else
    fprintf(st, "speed=0 (unrestricted)");
return SCPE_OK;
}

static t_stat dup_setspeed (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
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

/* SET/SHOW CORRUPTION processor */

static t_stat dup_showcorrupt (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);

if (dup_corruption[dup])
    fprintf(st, "Corruption=%d milligulps (%.1f%% of messages processed)", dup_corruption[dup], ((double)dup_corruption[dup])/10.0);
else
    fprintf(st, "No Corruption");
return SCPE_OK;
}

static t_stat dup_setcorrupt (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);
t_stat r;
int32 appetite;

if (cptr == NULL)
    return SCPE_ARG;
appetite = (int32) get_uint (cptr, 10, 999, &r);
if (r != SCPE_OK)
    return r;
dup_corruption[dup] = appetite;
return SCPE_OK;
}

/* SET/SHOW W3 processor */

static t_stat dup_show_W3 (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);

if (dup_W3[dup])
    fprintf(st, "W3 Jumper Installed");
else
    fprintf(st, "W3 Jumper Removed");
return SCPE_OK;
}

static t_stat dup_set_W3 (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);

dup_W3[dup] = val;
return SCPE_OK;
}

/* SET/SHOW W5 processor */

static t_stat dup_show_W5 (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);

if (dup_W5[dup])
    fprintf(st, "W5 Jumper Installed");
else
    fprintf(st, "W5 Jumper Removed");
return SCPE_OK;
}

static t_stat dup_set_W5 (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);

dup_W5[dup] = val;
return SCPE_OK;
}

/* SET/SHOW W6 processor */

static t_stat dup_show_W6 (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);

if (dup_W6[dup])
    fprintf(st, "W6 Jumper Installed");
else
    fprintf(st, "W6 Jumper Removed");
return SCPE_OK;
}

static t_stat dup_set_W6 (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
DEVICE *dptr = DUPDPTR;
int32 dup = (int32)(uptr-dptr->units);

dup_W6[dup] = val;
return SCPE_OK;
}

/* SET LINES processor */

static t_stat dup_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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
dup_desc.ldsc = dup_ldsc = (TMLN *)realloc(dup_ldsc, newln*sizeof(*dup_ldsc));
for (l=dup_desc.lines; l < newln; l++) {
    memset (&dup_ldsc[l], 0, sizeof(*dup_ldsc));
    dup_units[l] = dup_unit_template;
    }
dup_units[newln] = dup_poll_unit_template;
dup_desc.lines = newln;
dup_desc.uptr = dptr->units + newln;                /* Identify polling unit */
dptr->numunits = newln + 1;
return dup_reset (dptr);                            /* setup lines and auto config */
}

static t_stat dup_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
const char helpString[] =
 /* The '*'s in the next line represent the standard text width of a help line */
     /****************************************************************************/
    " The %D11 is a single-line, program controlled, double buffered\n"
    " communications device designed to interface the %1s system to a\n"
    " serial synchronous line. The original hardware is capable of handling\n"
    " a wide variety of protocols, including byte oriented protocols, such\n"
    " as DDCMP and BISYNC and bit-oriented protocols such as SDLC, HDLC\n"
    " and ADCCP.  The emulated device currently only supports connections\n"
    " using the DDCMP protocol.\n\n"
    " The %D11 is ideally suited for interfacing the %1s system\n"
    " to medium-speed synchronous lines for remote batch, remote data\n"
    " collection, remote concentration and network applications. Multiple\n"
    " %D11's on a %1s allow its use in applications requiring several\n"
    " synchronous lines.\n\n"
    " The %D11 is capable of transmitting data at the maximum speed of\n"
    " 9600 baud.  The emulated device can move data at significantly faster\n"
    " data rates.  The maximum emulated rate is dependent on the host CPU's\n"
    " available cycles.\n"
    "1 Hardware Description\n"
    " The %1s %D11 consists of a microprocessor module and a synchronous line unit\n"
    " module.\n"
    "2 $Registers\n"
    "\n"
    " These registers contain the emulated state of the device.  These values\n"
    " don't necessarily relate to any detail of the original device being\n"
    " emulated but are merely internal details of the emulation.\n"
    "1 Configuration\n"
    " A %D device is configured with various simh SET and ATTACH commands\n"
    "2 $Set commands\n"
    "3 Lines\n"
    " A maximum of %2s %D11 devices can be emulated concurrently in the %S\n"
    " simulator. The number of simulated %D devices or lines can be\n"
    " specified with command:\n"
    "\n"
    "+sim> SET %D LINES=n\n"
    "3 Peer\n"
    " To set the host and port to which data is to be transmitted use the\n"
    " following command:\n"
    "\n"
    "+sim> SET %U PEER=host:port\n"
    "3 Connectpoll\n"
    " The minimum interval between attempts to connect to the other side is set\n"
    " using the following command:\n"
    "\n"
    "+sim> SET %U CONNECTPOLL=n\n"
    "\n"
    " Where n is the number of seconds. The default is %3s seconds.\n"
    "3 Speed\n"
    " If you want to experience the actual data rates of the physical hardware\n"
    " you can set the bit rate of the simulated line can be set using the\n"
    " following command:\n"
    "\n"
    "+sim> SET %U SPEED=n\n"
    "\n"
    " Where n is the number of data bits per second that the simulated line\n"
    " runs at.  In practice this is implemented as a delay while transmitting\n"
    " bytes to the socket.  Use a value of zero to run at full speed with no\n"
    " artificial throttling.\n"
    "3 Corruption\n"
    " Corruption Troll - the DDCMP emulation includes the ability to enable a\n"
    " process that will intentionally drop or corrupt some messages.  This\n"
    " emulates the less-than-perfect communications lines encountered in the\n"
    " real world, and enables network monitoring software to see non-zero error\n"
    " counters.\n"
    "\n"
    " The troll selects messages with a probablility selected by the SET %U\n"
    " CORRUPT command.  The units are 0.1%%; that is, a value of 1 means that\n"
    " every message has a 1/1000 chance of being selected to be corrupted\n"
    " or discarded.\n"
     /****************************************************************************/
#define DUP_HLP_ATTACH "Configuration Attach"
    "2 Attach\n"
    " The communication line performs input and output through a TCP session\n"
    " (or UDP session) connected to a user-specified port.  The ATTACH command\n"
    " specifies the port to be used as well as the peer address:\n"
    "\n"
    "+sim> ATTACH %U {interface:}port{,UDP},Connect=peerhost:port\n"
    "\n"
    " where port is a decimal number between 1 and 65535 that is not being\n"
    " used for other TCP/IP activities.\n"
    "\n"
    " Specifying symmetric attach configuration (with both a listen port and\n"
    " a peer address) will cause the side receiving an incoming\n"
    " connection to validate that the connection actually comes from the\n"
    " connecction destination system.\n"
    " A symmetric attach configuration is required when using UDP packet\n"
    " transport.\n"
    "\n"
    " The default connection uses TCP transport between the local system and\n"
    " the peer.  Alternatively, UDP can be used by specifying UDP on the\n"
    " ATTACH command.\n"
    "\n"
    "2 Examples\n"
    " To configure two simulators to talk to each other use the following\n"
    " example:\n"
    " \n"
    " Machine 1\n"
    "+sim> SET %D ENABLE\n"
    "+sim> ATTACH %U 1111,connect=LOCALHOST:2222\n"
    " \n"
    " Machine 2\n"
    "+sim> SET %D ENABLE\n"
    "+sim> ATTACH %U 2222,connect=LOCALHOST:1111\n"
    "\n"
    "1 Monitoring\n"
    " The %D device and %U line configuration and state can be displayed with\n"
    " one of the available show commands.\n"
    "2 $Show commands\n"
    "1 Diagnostics\n"
    " Corruption Troll - the DDCMP emulation includes a process that will\n"
    " intentionally drop or corrupt some messages.  This emulates the\n"
    " less-than-perfect communications lines encountered in the real world,\n"
    " and enables network monitoring software to see non-zero error counters.\n"
    "\n"
    " The troll selects messages with a probablility selected by the SET %U\n"
    " CORRUPT command.  The units are 0.1%%; that is, a value of 1 means that\n"
    " every message has a 1/1000 chance of being selected to be corrupted\n"
    " or discarded.\n"
    "1 Restrictions\n"
    " Real hardware synchronous connections could operate in Multi-Point mode.\n"
    " Multi-Point mode was a way of sharing a single wire with multiple\n"
    " destination systems or devices.  Multi-Point mode is not currently\n"
    " emulated by this or other simulated synchronous devices.\n"
    "\n"
    "1 Implementation\n"
    " A real %D11 transports host generated protocol implemented data via a\n"
    " synchronous connection, the emulated device makes a TCP (or UDP)\n"
    " connection to another emulated device which either speaks DDCMP over the\n"
    " TCP connection directly, or interfaces to a simulated computer where the\n"
    " operating system speaks the DDCMP protocol on the wire.\n"
    "\n"
    " The %D11 can be used for point-to-point DDCMP connections carrying\n"
    " DECnet and other types of networking, e.g. from ULTRIX or DSM.\n"
    "1 Debugging\n"
    " The simulator has a number of debug options, these are:\n"
    "\n"
    "++REG     Shows whenever a CSR is programatically read or written\n"
    "++++and the current value.\n"
    "++INT     Shows Interrupt activity.\n"
    "++PKT     Shows Packet activity.\n"
    "++XMT     Shows Transmitted data.\n"
    "++RCV     Shows Received data.\n"
    "++MDM     Shows Modem Signal Transitions.\n"
    "++CON     Shows connection activities.\n"
    "++TRC     Shows routine call traces.\n"
    "++ASY     Shows Asynchronous activities.\n"
    "\n"
    " To get a full trace use\n"
    "\n"
    "+sim> SET %D DEBUG\n"
    "\n"
    " However it is recommended to use the following when sending traces:\n"
    "\n"
    "+sim> SET %D DEBUG=REG;PKT;XMT;RCV;CON\n"
    "\n"
    "1 Related Devices\n"
    " The %D11 can facilitate communication with other simh simulators which\n"
    " have emulated synchronous network devices available.  These include\n"
    " the following:\n"
    "\n"
    "++DUP11*       Unibus PDP11 simulators\n"
    "++DPV11*       Qbus PDP11 simulators\n"
    "++KDP11*       Unibus PDP11 simulators and PDP10 simulators\n"
    "++DMR11        Unibus PDP11 simulators and Unibus VAX simulators\n"
    "++DMC11        Unibus PDP11 simulators and Unibus VAX simulators\n"
    "++DMP11        Unibus PDP11 simulators and Unibus VAX simulators\n"
    "++DMV11        Qbus VAX simulators\n"
    "\n"
    "++* Indicates systems which have OS provided DDCMP implementations.\n"
    ;
char busname[16];
char devcount[16];
char connectpoll[16];

sprintf (busname, UNIBUS ? "Unibus" : "Qbus");
sprintf (devcount, "%d", DUP_LINES);
sprintf (connectpoll, "%d", DUP_CONNECT_POLL);

return scp_help (st, dptr, uptr, flag, helpString, cptr, busname, devcount, connectpoll);
}

static t_stat dup_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
return dup_help (st, dptr, uptr, flag, DUP_HLP_ATTACH);
}

static const char *dup_description (DEVICE *dptr)
{
return (UNIBUS) ? "DUP11 bit synchronous interface" :
                  "DPV11 bit synchronous interface";
}
