/* s100_2sio: MITS Altair serial I/O card

   Copyright (c) 2020, Patrick Linstruth

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

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.


   The 88-2 Serial Input/Output Board (88-2SIO) is designed around an
   Asynchronous Communications Interface Adapter (ACIA).

   The card had up to two physical I/O ports which could be connected
   to any serial I/O device that would connect to a current loop,
   RS232, or TTY interface.  Available baud rates were jumper
   selectable for each port from 110 to 9600.

   All I/O is via programmed I/O.  Each each port has a status port
   and a data port.  A write to the status port can select some
   options for the device (0x03 will reset the port). A read of the
   status port gets the port status:

   +---+---+---+---+---+---+---+---+
   | R   P   V   F   C   D   O   I |
   +---+---+---+---+---+---+---+---+

   I - A 1 in this bit position means a character has been received
       on the data port and is ready to be read.
   O - A 1 in this bit means the port is ready to receive a character
       on the data port and transmit it out over the serial line.
   D - A 1 in this bit means Data Carrier Detect is present.
   C - A 1 in this bit means Clear to Send is present.
   F - A 1 in this bit means a Framing Error has occurred.
   V - A 1 in this bit means an Overrun has occurred.
   P - A 1 in this bit means a Parity Error has occurred.
   R - A 1 in this bit means an Interrupt has occurred.

   A read to the data port gets the buffered character, a write
   to the data port writes the character to the device.

   The following are excerpts from Computer Notes, Volume 2, Issue 8,
   Jan-Feb '77:

   GLITCHES
   Q&A from the Repair Department
   By Bruce Fowler

   We get many calls on how to interface terminals to the 2SIO. The
   problem is that the Asynchronous Communications Interface Adapter's
   (ACIA) handshaking signals make interfacing with the 2SIO a
   somewhat complicated matter. An explaination of the signals and
   their function should make the job easier. The three handshaking
   signals--Data Carrier Detect (DCD), Request to Send (RTS) and
   Clear to Send (CTS)--permit limited control of a modem or
   peripheral. RTS is an output signal, and DCD and CTS are input
   signals.

   Data will only leave the ACIA when CTS is active.

   The ACIA will receive data only when DCD is active. DCD is normally
   used with modems. As long as DCD is inactive, the ACIA's receiver
   section is inhibited and no data can be received by the ACIA.

   Information from the two input signals, CTS and DCD, is present in
   the ACIA status register. Bit 2 represents *DCD, and bit 3 repre-
   sents *CTS. When bit 2 is high, DCD is inactive. When bit 3 is high,
   CTS is inactive. When bit 2 goes low, valid data is sent to the ACIA.
   When bit 3 goes low, data can be transmitted.

   * = Active Low
*/

#include <stdio.h>

#include "altairz80_defs.h"
#include "sim_tmxr.h"

#define M2SIO_NAME  "MITS 88-2SIO SERIAL ADAPTER"
#define M2SIO0_SNAME "M2SIO0"
#define M2SIO1_SNAME "M2SIO1"

#define M2SIO_WAIT         500           /* Service Wait Interval */

#define M2SIO0_IOBASE      0x10
#define M2SIO0_IOSIZE      2
#define M2SIO1_IOBASE      0x12
#define M2SIO1_IOSIZE      2

#define M2SIO_RDRF        0x01           /* Receive Data Register Full   */
#define M2SIO_TDRE        0x02           /* Transmit Data Register Empty */
#define M2SIO_DCD         0x04           /* Data Carrier Detect          */
#define M2SIO_CTS         0x08           /* Clear to Send                */
#define M2SIO_FE          0x10           /* Framing Error                */
#define M2SIO_OVRN        0x20           /* Overrun                      */
#define M2SIO_PE          0x40           /* Parity Error                 */
#define M2SIO_IRQ         0x80           /* Interrupt Request            */
#define M2SIO_RESET       0x03           /* Reset                        */
#define M2SIO_CLK1        0x00           /* Divide Clock by 1            */
#define M2SIO_CLK16       0x01           /* Divide Clock by 16           */
#define M2SIO_CLK64       0x02           /* Divide Clock by 64           */
#define M2SIO_72E         0x00           /* 7-2-E                        */
#define M2SIO_72O         0x04           /* 7-2-O                        */
#define M2SIO_71E         0x08           /* 7-1-E                        */
#define M2SIO_71O         0x0C           /* 7-1-O                        */
#define M2SIO_82N         0x10           /* 8-2-N                        */
#define M2SIO_81N         0x14           /* 8-1-N                        */
#define M2SIO_81E         0x18           /* 8-1-E                        */
#define M2SIO_81O         0x1C           /* 8-1-O                        */
#define M2SIO_FMTMSK      0x1c           /* Length, Parity, Stop Mask    */
#define M2SIO_RTSLTID     0x00           /* RTS Low, Xmit Int Disabled   */
#define M2SIO_RTSLTIE     0x20           /* RTS Low, Xmit Int Enabled    */
#define M2SIO_RTSHTID     0x40           /* RTS High, Xmit Int Disabled  */
#define M2SIO_RTSHTBR     0x60           /* RTS High, Xmit Break         */
#define M2SIO_RTSMSK      0x60           /* RTS Bit Mask                 */
#define M2SIO_RIE         0x80           /* Receive Int Enabled          */

#define M2SIO_BAUD        9600           /* Default baud rate            */

/* Debug flags */
#define STATUS_MSG        (1 << 0)
#define ERROR_MSG         (1 << 1)
#define VERBOSE_MSG       (1 << 2)

/* IO Read/Write */
#define IO_RD            0x00            /* IO Read  */
#define IO_WR            0x01            /* IO Write */

typedef struct {
    PNP_INFO pnp;        /* Must be first    */
    int32 port;          /* Port 0 or 1      */
    int32 conn;          /* Connected Status */
    TMLN *tmln;          /* TMLN pointer     */
    TMXR *tmxr;          /* TMXR pointer     */
    int32 baud;          /* Baud rate        */
    int32 rts;           /* RTS Status       */
    int32 rxb;           /* Receive Buffer   */
    int32 txb;           /* Transmit Buffer  */
    int32 txp;           /* Transmit Pending */
    int32 stb;           /* Status Buffer    */
    int32 ctb;           /* Control Buffer   */
    int32 rie;           /* Rx Int Enable    */
    int32 tie;           /* Tx Int Enable    */
} M2SIO_CTX;

extern uint32 getClockFrequency(void);
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);


static const char* m2sio_description(DEVICE *dptr);
static t_stat m2sio_svc(UNIT *uptr);
static t_stat m2sio_reset(DEVICE *dptr, int32 (*routine)(const int32, const int32, const int32));
static t_stat m2sio0_reset(DEVICE *dptr);
static t_stat m2sio1_reset(DEVICE *dptr);
static t_stat m2sio_attach(UNIT *uptr, CONST char *cptr);
static t_stat m2sio_detach(UNIT *uptr);
static t_stat m2sio_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat m2sio_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc);
static t_stat m2sio_config_line(UNIT *uptr);
static int32 m2sio0_io(int32 addr, int32 io, int32 data);
static int32 m2sio1_io(int32 addr, int32 io, int32 data);
static int32 m2sio_io(DEVICE *dptr, int32 addr, int32 io, int32 data);
static int32 m2sio_stat(DEVICE *dptr, int32 io, int32 data);
static int32 m2sio_data(DEVICE *dptr, int32 io, int32 data);

/* Debug Flags */
static DEBTAB m2sio_dt[] = {
    { "STATUS",         STATUS_MSG,         "Status messages"  },
    { "ERROR",          ERROR_MSG,          "Error messages"  },
    { "VERBOSE",        VERBOSE_MSG,        "Verbose messages"  },
    { NULL,             0                   }
};

/* Terminal multiplexer library descriptors */

static TMLN m2sio0_tmln[] = {         /* line descriptors */
    { 0 }
};

static TMLN m2sio1_tmln[] = {         /* line descriptors */
    { 0 }
};

static TMXR m2sio0_tmxr = {                     /* multiplexer descriptor */
    1,                                          /* number of terminal lines */
    0,                                          /* listening port (reserved) */
    0,                                          /* master socket  (reserved) */
    m2sio0_tmln,                                /* line descriptor array */
    NULL,                                       /* line connection order */
    NULL                                        /* multiplexer device (derived internally) */
};

static TMXR m2sio1_tmxr = {                     /* multiplexer descriptor */
    1,                                          /* number of terminal lines */
    0,                                          /* listening port (reserved) */
    0,                                          /* master socket  (reserved) */
    m2sio1_tmln,                                /* line descriptor array */
    NULL,                                       /* line connection order */
    NULL                                        /* multiplexer device (derived internally) */
};


#define UNIT_V_M2SIO_DTR      (UNIT_V_UF + 0)     /* DTR follows RTS                */
#define UNIT_M2SIO_DTR        (1 << UNIT_V_M2SIO_DTR)
#define UNIT_V_M2SIO_DCD      (UNIT_V_UF + 1)     /* Force DCD active low           */
#define UNIT_M2SIO_DCD        (1 << UNIT_V_M2SIO_DCD)

static MTAB m2sio_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets MITS 2SIO base I/O address"   },
    { UNIT_M2SIO_DTR,       UNIT_M2SIO_DTR,     "DTR",    "DTR",    NULL, NULL, NULL,
        "DTR follows RTS" },
    { UNIT_M2SIO_DTR,       0,                  "NODTR",  "NODTR",  NULL, NULL, NULL,
        "DTR does not follow RTS (default)" },
    { UNIT_M2SIO_DCD,       UNIT_M2SIO_DCD,     "DCD",    "DCD",    NULL, NULL, NULL,
        "Force DCD active low" },
    { UNIT_M2SIO_DCD,       0,                  "NODCD",  "NODCD",  NULL, NULL, NULL,
        "DCD follows status line (default)" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,  0,   "BAUD",  "BAUD",  &m2sio_set_baud, &m2sio_show_baud,
        NULL, "Set baud rate (default=9600)" },
    { 0 }
};

static M2SIO_CTX m2sio0_ctx = {{0, 0, M2SIO0_IOBASE, M2SIO0_IOSIZE}, 0, 0, m2sio0_tmln, &m2sio0_tmxr, M2SIO_BAUD, 1};
static M2SIO_CTX m2sio1_ctx = {{0, 0, M2SIO1_IOBASE, M2SIO1_IOSIZE}, 1, 0, m2sio1_tmln, &m2sio1_tmxr, M2SIO_BAUD, 1};

static UNIT m2sio0_unit[] = {
        { UDATA (&m2sio_svc, UNIT_ATTABLE | UNIT_DISABLE, 0), M2SIO_WAIT },
};
static UNIT m2sio1_unit[] = {
        { UDATA (&m2sio_svc, UNIT_ATTABLE | UNIT_DISABLE, 0), M2SIO_WAIT },
};

static REG m2sio0_reg[] = {
    { HRDATAD (M2STA0, m2sio0_ctx.stb, 8, "2SIO port 0 status register"), },
    { HRDATAD (M2CTL0, m2sio0_ctx.ctb, 8, "2SIO port 0 control register"), },
    { HRDATAD (M2RXD0, m2sio0_ctx.rxb, 8, "2SIO port 0 rx data buffer"), },
    { HRDATAD (M2TXD0, m2sio0_ctx.txb, 8, "2SIO port 0 tx data buffer"), },
    { HRDATAD (M2TXP0, m2sio0_ctx.txp, 8, "2SIO port 0 tx data pending"), },
    { FLDATAD (M2CON0, m2sio0_ctx.conn, 0, "2SIO port 0 connection status"), },
    { FLDATAD (M2RIE0, m2sio0_ctx.rie, 0, "2SIO port 0 receive interrupt enable"), },
    { FLDATAD (M2TIE0, m2sio0_ctx.tie, 0, "2SIO port 0 transmit interrupt enable"), },
    { FLDATAD (M2RTS0, m2sio0_ctx.rts, 0, "2SIO port 0 RTS status (active low)"), },
    { FLDATAD (M2RDRF0, m2sio0_ctx.stb, 0, "2SIO port 0 RDRF status"), },
    { FLDATAD (M2TDRE0, m2sio0_ctx.stb, 1, "2SIO port 0 TDRE status"), },
    { FLDATAD (M2DCD0, m2sio0_ctx.stb, 2, "2SIO port 0 DCD status (active low)"), },
    { FLDATAD (M2CTS0, m2sio0_ctx.stb, 3, "2SIO port 0 CTS status (active low)"), },
    { FLDATAD (M2OVRN0, m2sio0_ctx.stb, 4, "2SIO port 0 OVRN status"), },
    { DRDATAD (M2WAIT0, m2sio0_unit[0].wait, 32, "2SIO port 0 wait cycles"), },
    { NULL }
};
static REG m2sio1_reg[] = {
    { HRDATAD (M2STA1, m2sio1_ctx.stb, 8, "2SIO port 1 status buffer"), },
    { HRDATAD (M2CTL1, m2sio1_ctx.ctb, 8, "2SIO port 1 control register"), },
    { HRDATAD (M2RXD1, m2sio1_ctx.rxb, 8, "2SIO port 1 rx data buffer"), },
    { HRDATAD (M2TXD1, m2sio1_ctx.txb, 8, "2SIO port 1 tx data buffer"), },
    { HRDATAD (M2TXP1, m2sio1_ctx.txp, 8, "2SIO port 1 tx data pending"), },
    { FLDATAD (M2CON1, m2sio1_ctx.conn, 0, "2SIO port 1 connection status"), },
    { FLDATAD (M2RIE1, m2sio1_ctx.rie, 0, "2SIO port 1 receive interrupt enable"), },
    { FLDATAD (M2TIE1, m2sio1_ctx.tie, 0, "2SIO port 1 transmit interrupt enable"), },
    { FLDATAD (M2RTS1, m2sio1_ctx.rts, 0, "2SIO port 1 RTS status (active low)"), },
    { FLDATAD (M2RDRF1, m2sio1_ctx.stb, 0, "2SIO port 1 RDRF status"), },
    { FLDATAD (M2TDRE1, m2sio1_ctx.stb, 1, "2SIO port 1 TDRE status"), },
    { FLDATAD (M2DCD1, m2sio1_ctx.stb, 2, "2SIO port 1 DCD status (active low)"), },
    { FLDATAD (M2CTS1, m2sio1_ctx.stb, 3, "2SIO port 1 CTS status (active low)"), },
    { FLDATAD (M2OVRN1, m2sio1_ctx.stb, 4, "2SIO port 1 OVRN status"), },
    { DRDATAD (M2WAIT1, m2sio1_unit[0].wait, 32, "2SIO port 1 wait cycles"), },
    { NULL }
};

DEVICE m2sio0_dev = {
    M2SIO0_SNAME,  /* name */
    m2sio0_unit,   /* unit */
    m2sio0_reg,    /* registers */
    m2sio_mod,    /* modifiers */
    1,            /* # units */
    10,           /* address radix */
    31,           /* address width */
    1,            /* address increment */
    8,            /* data radix */
    8,            /* data width */
    NULL,         /* examine routine */
    NULL,         /* deposit routine */
    &m2sio0_reset, /* reset routine */
    NULL,         /* boot routine */
    &m2sio_attach,         /* attach routine */
    &m2sio_detach,         /* detach routine */
    &m2sio0_ctx,           /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX),  /* flags */
    0,                            /* debug control */
    m2sio_dt,                           /* debug flags */
    NULL,                                 /* mem size routine */
    NULL,                                 /* logical name */
    NULL,                                 /* help */
    NULL,                                 /* attach help */
    NULL,                                 /* context for help */
    &m2sio_description                    /* description */
};

DEVICE m2sio1_dev = {
    M2SIO1_SNAME,  /* name */
    m2sio1_unit,   /* unit */
    m2sio1_reg,    /* registers */
    m2sio_mod,    /* modifiers */
    1,            /* # units */
    10,           /* address radix */
    31,           /* address width */
    1,            /* address increment */
    8,            /* data radix */
    8,            /* data width */
    NULL,         /* examine routine */
    NULL,         /* deposit routine */
    &m2sio1_reset, /* reset routine */
    NULL,         /* boot routine */
    &m2sio_attach,         /* attach routine */
    &m2sio_detach,         /* detach routine */
    &m2sio1_ctx,           /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX),  /* flags */
    0,                            /* debug control */
    m2sio_dt,                           /* debug flags */
    NULL,                                 /* mem size routine */
    NULL,                                 /* logical name */
    NULL,                                 /* help */
    NULL,                                 /* attach help */
    NULL,                                 /* context for help */
    &m2sio_description                    /* description */
};

static const char* m2sio_description(DEVICE *dptr)
{
    return M2SIO_NAME;
}

static t_stat m2sio0_reset(DEVICE *dptr)
{
    return(m2sio_reset(dptr, &m2sio0_io));
}

static t_stat m2sio1_reset(DEVICE *dptr)
{
    return(m2sio_reset(dptr, &m2sio1_io));
}

static t_stat m2sio_reset(DEVICE *dptr, int32 (*routine)(const int32, const int32, const int32))
{
    M2SIO_CTX *xptr;
    int32 c;

    xptr = dptr->ctxt;

    /* Connect/Disconnect I/O Ports at base address */
    if(sim_map_resource(xptr->pnp.io_base, xptr->pnp.io_size, RESOURCE_TYPE_IO, routine, dptr->name, dptr->flags & DEV_DIS) != 0) {
        sim_debug(ERROR_MSG, dptr, "error mapping I/O resource at 0x%02x.\n", xptr->pnp.io_base);
        return SCPE_ARG;
    }

    /* Set DEVICE for this UNIT */
    dptr->units[0].dptr = dptr;
    c = getClockFrequency() / 5;
    dptr->units[0].wait = (c && c < 1000) ? c : 1000;

    /* Enable TMXR modem control passthru */
    tmxr_set_modem_control_passthru(xptr->tmxr);

    /* Reset status registers */
    xptr->stb = 0;
    xptr->txp = 0;

    if (!(dptr->flags & DEV_DIS)) {
        sim_activate(&dptr->units[0], dptr->units[0].wait);
    } else {
        sim_cancel(&dptr->units[0]);
    }

    sim_debug(STATUS_MSG, dptr, "reset adapter.\n");

    return SCPE_OK;
}

static t_stat m2sio_svc(UNIT *uptr)
{
    M2SIO_CTX *xptr;
    int32 c,s,stb;
    t_stat r;

    xptr = uptr->dptr->ctxt;

    /* Check for new incoming connection */
    if (uptr->flags & UNIT_ATT) {
        if (tmxr_poll_conn(xptr->tmxr) >= 0) {      /* poll connection */

            /* Clear DTR and RTS if serial port */
            if (xptr->tmln->serport) {
                tmxr_set_get_modem_bits(xptr->tmln, 0, TMXR_MDM_DTR | TMXR_MDM_RTS, NULL);
            }

            xptr->conn = 1;          /* set connected   */

            sim_debug(STATUS_MSG, uptr->dptr, "new connection.\n");
        }
    }

    /* Update incoming modem status bits */
    if (uptr->flags & UNIT_ATT) {
        tmxr_set_get_modem_bits(xptr->tmln, 0, 0, &s);
        stb = xptr->stb;
        xptr->stb &= ~M2SIO_CTS;
        xptr->stb |= (s & TMXR_MDM_CTS) ? 0 : M2SIO_CTS;     /* Active Low */
        if ((stb ^ xptr->stb) & M2SIO_CTS) {
            sim_debug(STATUS_MSG, uptr->dptr, "CTS state changed to %s.\n", (xptr->stb & M2SIO_CTS) ? "LOW" : "HIGH");
        }
        xptr->stb &= ~M2SIO_DCD;
        xptr->stb |= ((s & TMXR_MDM_DCD) || (uptr->flags & UNIT_M2SIO_DCD)) ? 0 : M2SIO_DCD;     /* Active Low */
        if ((stb ^ xptr->stb) & M2SIO_DCD) {
            sim_debug(STATUS_MSG, uptr->dptr, "DCD state changed to %s.\n", (xptr->stb & M2SIO_DCD) ? "LOW" : "HIGH");
        }

        /* Enable receiver if DCD is active low */
        xptr->tmln->rcve = !(xptr->stb & M2SIO_DCD);
    }

    /* TX data */
    if (xptr->txp) {
        if (uptr->flags & UNIT_ATT) {
            if (!(xptr->stb & M2SIO_CTS)) {    /* Active low */
                r = tmxr_putc_ln(xptr->tmln, xptr->txb);
                xptr->txp = 0;               /* Reset TX Pending */
            } else {
                r = SCPE_STALL;
            }
        } else {
            r = sim_putchar(xptr->txb);
            xptr->txp = 0;               /* Reset TX Pending */
        }

        if (r == SCPE_LOST) {
            xptr->conn = 0;          /* Connection was lost */
            sim_debug(STATUS_MSG, uptr->dptr, "lost connection.\n");
        }
    }

    /* Update TDRE if not set and no character pending */
    if (!xptr->txp && !(xptr->stb & M2SIO_TDRE)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_tx(xptr->tmxr);
            xptr->stb |= (tmxr_txdone_ln(xptr->tmln) && xptr->conn) ? M2SIO_TDRE : 0;
        } else {
            xptr->stb |= M2SIO_TDRE;
        }
    }

    /* Check for Data if RX buffer empty */
    if (!(xptr->stb & M2SIO_RDRF)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_rx(xptr->tmxr);

            c = tmxr_getc_ln(xptr->tmln);
        } else {
            c = sim_poll_kbd();
        }

        if (c & (TMXR_VALID | SCPE_KFLAG)) {
            xptr->rxb = c & 0xff;
            xptr->stb |= M2SIO_RDRF;
            xptr->stb &= ~(M2SIO_FE | M2SIO_OVRN | M2SIO_PE);
        }
    }

    /* Don't let TMXR clobber our wait time */
    uptr->wait = M2SIO_WAIT;

    sim_activate_abs(uptr, uptr->wait);

    return SCPE_OK;
}


/* Attach routine */
static t_stat m2sio_attach(UNIT *uptr, CONST char *cptr)
{
    M2SIO_CTX *xptr;
    t_stat r;

    xptr = uptr->dptr->ctxt;

    sim_debug(VERBOSE_MSG, uptr->dptr, "attach (%s).\n", cptr);

    if ((r = tmxr_attach(xptr->tmxr, uptr, cptr)) == SCPE_OK) {

        xptr->tmln->rcve = 1;

        sim_activate(uptr, uptr->wait);

        sim_debug(VERBOSE_MSG, uptr->dptr, "activated service.\n");
    }

    return r;
}


/* Detach routine */
static t_stat m2sio_detach(UNIT *uptr)
{
    M2SIO_CTX *xptr;

    sim_debug(VERBOSE_MSG, uptr->dptr, "detach.\n");

    if (uptr->flags & UNIT_ATT) {
        xptr = uptr->dptr->ctxt;

        sim_cancel(uptr);

        return (tmxr_detach(xptr->tmxr, uptr));
    }

    return SCPE_UNATT;
}

static t_stat m2sio_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    M2SIO_CTX *xptr;
    int32 baud;
    t_stat r = SCPE_ARG;

    xptr = uptr->dptr->ctxt;

    if (!(uptr->flags & UNIT_ATT)) {
        return SCPE_UNATT;
    }

    if (cptr != NULL) {
        if (sscanf(cptr, "%d", &baud)) {
            switch (baud) {
                case 110:
                case 150:
                case 300:
                case 1200:
                case 1800:
                case 2400:
                case 4800:
                case 9600:
                    xptr->baud = baud;
                    r = m2sio_config_line(uptr);

                    return r;
 
                default:
                    break;
            }
        }
    }

    return r;
}

static t_stat m2sio_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc)
{
    M2SIO_CTX *xptr;

    xptr = uptr->dptr->ctxt;

    if (uptr->flags & UNIT_ATT) {
        fprintf(st, "Baud rate: %d", xptr->baud);
    }

    return SCPE_OK;
}

static t_stat m2sio_config_line(UNIT *uptr)
{
    M2SIO_CTX *xptr;
    char config[20];
    char *fmt;
    t_stat r = SCPE_IERR;

    xptr = uptr->dptr->ctxt;

    if (xptr != NULL) {
        switch (xptr->ctb & M2SIO_FMTMSK) {
            case M2SIO_72E:
                fmt = "7E2";
                break;
            case M2SIO_72O:
                fmt = "7O2";
                break;
            case M2SIO_71E:
                fmt = "7E1";
                break;
            case M2SIO_71O:
                fmt = "7O1";
                break;
            case M2SIO_82N:
                fmt = "8N2";
                break;
            case M2SIO_81E:
                fmt = "8E1";
                break;
            case M2SIO_81O:
                fmt = "8O1";
                break;
            case M2SIO_81N:
            default:
                fmt = "8N1";
                break;
        }

        sprintf(config, "%d-%s", xptr->baud, fmt);

        r = tmxr_set_config_line(xptr->tmln, config);

        sim_debug(STATUS_MSG, uptr->dptr, "port configuration set to '%s'.\n", config);

        /*
        ** AltairZ80 and TMXR refuse to want to play together 
        ** nicely when the CLOCK register is set to anything
        ** other than 0.
        **
        ** This work-around is for those of us that may wish
        ** to run irrelevant, old software, that use TMXR and
        ** rely on some semblance of timing (Remote CP/M, BYE,
        ** RBBS, PCGET/PUT, Xmodem, MEX, Modem7, or most
        ** other communications software), on contemprary
        ** hardware.
        **
        ** Serial ports are self-limiting and sockets will run
        ** at the clocked CPU speed.
        */
        xptr->tmln->txbps = 0;   /* Get TMXR's timing out of our way */
        xptr->tmln->rxbps = 0;   /* Get TMXR's timing out of our way */
    }

    return r;
}

static int32 m2sio0_io(int32 addr, int32 io, int32 data)
{
    DEVICE *dptr;

    dptr = &m2sio0_dev;

    return(m2sio_io(dptr, addr, io, data));
}

static int32 m2sio1_io(int32 addr, int32 io, int32 data)
{
    DEVICE *dptr;

    dptr = &m2sio1_dev;

    return(m2sio_io(dptr, addr, io, data));
}

static int32 m2sio_io(DEVICE *dptr, int32 addr, int32 io, int32 data)
{
    int32 r;

    if (addr & 0x01) {
        r = m2sio_data(dptr, io, data);
    } else {
        r = m2sio_stat(dptr, io, data);
    }

    return(r);
}

static int32 m2sio_stat(DEVICE *dptr, int32 io, int32 data)
{
    M2SIO_CTX *xptr;
    int32 r,s;

    xptr = dptr->ctxt;

    if (io == IO_RD) {
        r = xptr->stb;
    } else {
        xptr->ctb = data & 0xff;    /* save control byte */

        /* Master Reset */
        if ((data & M2SIO_RESET) == M2SIO_RESET) {
            xptr->stb &= (M2SIO_CTS | M2SIO_DCD);           /* Reset status register */
            xptr->txp = 0;
            sim_debug(STATUS_MSG, dptr, "MC6850 master reset.\n");
        } else if (dptr->units[0].flags & UNIT_ATT) {
            /* Interrupt Enable */
            xptr->tie = (data & M2SIO_RIE) == M2SIO_RIE;           /* Receive enable  */
            xptr->rie = (data & M2SIO_RTSMSK) == M2SIO_RTSLTIE;    /* Transmit enable */

            /* RTS Control */
            s = TMXR_MDM_RTS;
            if (dptr->units[0].flags & UNIT_M2SIO_DTR) {
                s |= TMXR_MDM_DTR;
            }

            switch (data & M2SIO_RTSMSK) {
                case M2SIO_RTSLTIE:
                case M2SIO_RTSLTID:
                    tmxr_set_get_modem_bits(xptr->tmln, s, 0, NULL);
                    if (xptr->rts) {
                        sim_debug(STATUS_MSG, dptr, "RTS state changed to HIGH.\n");
                    }
                    xptr->rts = 0;    /* Active low */
                    break;

                case M2SIO_RTSHTID:
                case M2SIO_RTSHTBR:
                    tmxr_set_get_modem_bits(xptr->tmln, 0, s, NULL);
                    if (!xptr->rts) {
                        sim_debug(STATUS_MSG, dptr, "RTS state changed to LOW.\n");
                    }
                    xptr->rts = 1;    /* Active low */
                    break;

                default:
                    break;
            }

            /* Set data bits, parity and stop bits format */
            m2sio_config_line(&dptr->units[0]);
        }

        r = 0x00;
    }

    return(r);
}

static int32 m2sio_data(DEVICE *dptr, int32 io, int32 data)
{
    M2SIO_CTX *xptr;
    int32 r;

    xptr = dptr->ctxt;

    if (io == IO_RD) {
        r = xptr->rxb;
        xptr->stb &= ~(M2SIO_RDRF | M2SIO_FE | M2SIO_OVRN | M2SIO_PE);
    } else {
        xptr->txb = data;
        xptr->stb &= ~M2SIO_TDRE;
        xptr->txp = 1;
        r = 0x00;
    }

    return r;
}


