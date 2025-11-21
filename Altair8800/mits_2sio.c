/* mits_2sio.c: MITS Altair 8800 88-2SIO

   Copyright (c) 2025 Patrick A. Linstruth

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Patrick Linstruth shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   History:
   07-Nov-2025 Initial version
  
   ==================================================================
  
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
   somewhat complicated matter. An explanation of the signals and
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
   the ACIA status register. Bit 2 represents /DCD, and bit 3
   represents /CTS. When bit 2 is high, DCD is inactive. When bit 3 is high,
   CTS is inactive. When bit 2 goes low, valid data is sent to the ACIA.
   When bit 3 goes low, data can be transmitted.
 
  / = Active Low

*/

#include "sim_defs.h"
#include "sim_tmxr.h"
#include "altair8800_defs.h"
#include "s100_bus.h"
#include "mits_2sio.h"

#define M2SIO_NAME  "MITS 88-2SIO SERIAL ADAPTER"
#define M2SIO0_SNAME "M2SIO0"
#define M2SIO1_SNAME "M2SIO1"

#define M2SIO_PORTS        2

#define M2SIO_WAIT         250           /* Service Wait Interval */

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

static const char* m2sio_description(DEVICE *dptr);
static t_stat m2sio_svc(UNIT *uptr);
static t_stat m2sio_reset(DEVICE *dptr, int32 (*routine)(const int32, const int32, const int32));
static t_stat m2sio0_reset(DEVICE *dptr);
static t_stat m2sio1_reset(DEVICE *dptr);
static t_stat m2sio_attach(UNIT *uptr, const char *cptr);
static t_stat m2sio_detach(UNIT *uptr);
static t_stat m2sio_set_console(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat m2sio_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat m2sio_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc);
static t_stat m2sio_config_line(UNIT *uptr);
static t_stat m2sio_config_rts(DEVICE *dptr, char rts);
static int32 m2sio0_io(int32 addr, int32 io, int32 data);
static int32 m2sio1_io(int32 addr, int32 io, int32 data);
static int32 m2sio_io(DEVICE *dptr, int32 addr, int32 io, int32 data);
static int32 m2sio_stat(DEVICE *dptr, int32 io, int32 data);
static int32 m2sio_data(DEVICE *dptr, int32 io, int32 data);
static void m2sio_int(UNIT *uptr);
static int32 m2sio_map_kbdchar(UNIT *uptr, int32 ch);
static t_stat m2sio_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);

static M2SIO_REG m2sio0_reg;
static M2SIO_REG m2sio1_reg;

/* Debug Flags */
#define STATUS_MSG        (1 << 0)
#define IRQ_MSG           (1 << 1)
#define VERBOSE_MSG       (1 << 2)

/* Debug Table */
static DEBTAB m2sio_dt[] = {
    { "STATUS",         STATUS_MSG,         "Status messages"  },
    { "IRQ",            IRQ_MSG,            "Interrupt messages"  },
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


static MTAB m2sio_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets MITS 2SIO base I/O address"   },

    { UNIT_M2SIO_MAP,         0,                  "NOMAP",    "NOMAP",    NULL, NULL, NULL,
        "Do not map any character" }, /*  disable character mapping                         */
    { UNIT_M2SIO_MAP,         UNIT_M2SIO_MAP,  "MAP",      "MAP",      NULL, NULL, NULL,
        "Enable mapping of characters" }, /*  enable all character mapping                  */
    { UNIT_M2SIO_UPPER,       0,                "NOUPPER",   "NOUPPER",      NULL, NULL, NULL,
        "Console input remains unchanged" }, /* do not change case of input characters      */
    { UNIT_M2SIO_UPPER,       UNIT_M2SIO_UPPER, "UPPER",    "UPPER",    NULL, NULL, NULL,
        "Convert console input to upper case" }, /* change input characters to upper case   */
    { UNIT_M2SIO_BS,          0,                "BS",       "BS",       NULL, NULL, NULL,
        "Map delete to backspace" }, /* map delete to backspace                             */
    { UNIT_M2SIO_BS,          UNIT_M2SIO_BS,    "DEL",      "DEL",      NULL, NULL, NULL,
        "Map backspace to delete" }, /* map backspace to delete                             */
    { UNIT_M2SIO_DTR,       UNIT_M2SIO_DTR,     "DTR",       "DTR",       NULL, NULL, NULL,
        "DTR follows RTS" },
    { UNIT_M2SIO_DTR,       0,                  "NODTR",     "NODTR",     NULL, NULL, NULL,
        "DTR does not follow RTS (default)" },
    { UNIT_M2SIO_DCD,       UNIT_M2SIO_DCD,     "DCD",       "DCD",       NULL, NULL, NULL,
        "Force DCD active low" },
    { UNIT_M2SIO_DCD,       0,                  "NODCD",     "NODCD",     NULL, NULL, NULL,
        "DCD follows status line (default)" },
    { UNIT_M2SIO_CTS,       UNIT_M2SIO_CTS,     "CTS",       "CTS",       NULL, NULL, NULL,
        "Force CTS active low" },
    { UNIT_M2SIO_CTS,       0,                  "NOCTS",     "NOCTS",     NULL, NULL, NULL,
        "CTS follows status line (default)" },

    { MTAB_XTD | MTAB_VUN,  UNIT_M2SIO_CONSOLE, NULL, "CONSOLE",   &m2sio_set_console, NULL, NULL, "Set as CONSOLE" },
    { MTAB_XTD | MTAB_VUN,  0,                  NULL, "NOCONSOLE", &m2sio_set_console, NULL, NULL, "Remove as CONSOLE" },

    { MTAB_XTD|MTAB_VDV|MTAB_VALR,  0,   "BAUD",  "BAUD",  &m2sio_set_baud, &m2sio_show_baud,
        NULL, "Set baud rate (default=9600)" },
    { 0 }
};

static RES m2sio0_res = { M2SIO0_IOBASE, M2SIO0_IOSIZE, 0, 0, &m2sio0_tmxr };
static RES m2sio1_res = { M2SIO1_IOBASE, M2SIO1_IOSIZE, 0, 0, &m2sio1_tmxr };

static UNIT unit0[] = {
        { UDATA (&m2sio_svc, UNIT_ATTABLE | UNIT_M2SIO_MAP | UNIT_M2SIO_CONSOLE | UNIT_M2SIO_DCD | UNIT_M2SIO_CTS , 0), M2SIO_WAIT },
};

static UNIT unit1[] = {
        { UDATA (&m2sio_svc, UNIT_ATTABLE | UNIT_M2SIO_DCD | UNIT_M2SIO_CTS, 0), M2SIO_WAIT },
};

static REG reg0[] = {
    { HRDATAD (M2STA0, m2sio0_reg.stb, 8, "2SIO port 0 status register"), },
    { HRDATAD (M2CTL0, m2sio0_reg.ctb, 8, "2SIO port 0 control register"), },
    { HRDATAD (M2RXD0, m2sio0_reg.rxb, 8, "2SIO port 0 rx data buffer"), },
    { HRDATAD (M2TXD0, m2sio0_reg.txb, 8, "2SIO port 0 tx data buffer"), },
    { FLDATAD (M2TXP0, m2sio0_reg.txp, 0, "2SIO port 0 tx data pending"), },
    { FLDATAD (M2CON0, m2sio0_reg.conn, 0, "2SIO port 0 connection status"), },
    { FLDATAD (M2RIE0, m2sio0_reg.rie, 0, "2SIO port 0 receive interrupt enable"), },
    { FLDATAD (M2TIE0, m2sio0_reg.tie, 0, "2SIO port 0 transmit interrupt enable"), },
    { FLDATAD (M2RTS0, m2sio0_reg.rts, 0, "2SIO port 0 RTS status (active low)"), },
    { FLDATAD (M2RDRF0, m2sio0_reg.stb, 0, "2SIO port 0 RDRF status"), },
    { FLDATAD (M2TDRE0, m2sio0_reg.stb, 1, "2SIO port 0 TDRE status"), },
    { FLDATAD (M2DCD0, m2sio0_reg.stb, 2, "2SIO port 0 DCD status (active low)"), },
    { FLDATAD (M2CTS0, m2sio0_reg.stb, 3, "2SIO port 0 CTS status (active low)"), },
    { FLDATAD (M2OVRN0, m2sio0_reg.stb, 4, "2SIO port 0 OVRN status"), },
    { FLDATAD (DCDL0, m2sio0_reg.dcdl, 0, "2SIO port 0 DCD latch"), },
    { DRDATAD (M2WAIT0, unit0[0].wait, 32, "2SIO port 0 wait cycles"), },
    { FLDATAD (M2INTEN0, m2sio0_reg.intenable, 1, "2SIO port 0 Global vectored interrupt enable"), },
    { DRDATAD (M2VEC0, m2sio0_reg.intvector, 8, "2SIO port 0 interrupt vector"), },
    { HRDATAD (M2DBVAL0, m2sio0_reg.databus, 8, "2SIO port 0 data bus value"), },
    { NULL }
};
static REG reg1[] = {
    { HRDATAD (M2STA1, m2sio1_reg.stb, 8, "2SIO port 1 status buffer"), },
    { HRDATAD (M2CTL1, m2sio1_reg.ctb, 8, "2SIO port 1 control register"), },
    { HRDATAD (M2RXD1, m2sio1_reg.rxb, 8, "2SIO port 1 rx data buffer"), },
    { HRDATAD (M2TXD1, m2sio1_reg.txb, 8, "2SIO port 1 tx data buffer"), },
    { FLDATAD (M2TXP1, m2sio1_reg.txp, 0, "2SIO port 1 tx data pending"), },
    { FLDATAD (M2CON1, m2sio1_reg.conn, 0, "2SIO port 1 connection status"), },
    { FLDATAD (M2RIE1, m2sio1_reg.rie, 0, "2SIO port 1 receive interrupt enable"), },
    { FLDATAD (M2TIE1, m2sio1_reg.tie, 0, "2SIO port 1 transmit interrupt enable"), },
    { FLDATAD (M2RTS1, m2sio1_reg.rts, 0, "2SIO port 1 RTS status (active low)"), },
    { FLDATAD (M2RDRF1, m2sio1_reg.stb, 0, "2SIO port 1 RDRF status"), },
    { FLDATAD (M2TDRE1, m2sio1_reg.stb, 1, "2SIO port 1 TDRE status"), },
    { FLDATAD (M2DCD1, m2sio1_reg.stb, 2, "2SIO port 1 DCD status (active low)"), },
    { FLDATAD (M2CTS1, m2sio1_reg.stb, 3, "2SIO port 1 CTS status (active low)"), },
    { FLDATAD (M2OVRN1, m2sio1_reg.stb, 4, "2SIO port 1 OVRN status"), },
    { FLDATAD (DCDL1, m2sio1_reg.dcdl, 0, "2SIO port 1 DCD latch"), },
    { DRDATAD (M2WAIT1, unit1[0].wait, 32, "2SIO port 1 wait cycles"), },
    { FLDATAD (M2INTEN1, m2sio1_reg.intenable, 1, "2SIO port 1 Global vectored interrupt enable"), },
    { DRDATAD (M2VEC1, m2sio1_reg.intvector, 8, "2SIO port 1 interrupt vector"), },
    { HRDATAD (M2DBVAL1, m2sio1_reg.databus, 8, "2SIO port 1 data bus value"), },
    { NULL }
};

DEVICE m2sio0_dev = {
    M2SIO0_SNAME,       /* name */
    unit0,        /* unit */
    reg0,         /* registers */
    m2sio_mod,          /* modifiers */
    1,                  /* # units */
    ADDRRADIX,          /* address radix */
    ADDRWIDTH,          /* address width */
    1,                  /* address increment */
    DATARADIX,          /* data radix */
    DATAWIDTH,          /* data width */
    NULL,               /* examine routine */
    NULL,               /* deposit routine */
    &m2sio0_reset,      /* reset routine */
    NULL,               /* boot routine */
    &m2sio_attach,      /* attach routine */
    &m2sio_detach,      /* detach routine */
    &m2sio0_res,        /* context */
    (DEV_DISABLE | DEV_DEBUG | DEV_MUX),  /* flags */
    0,                  /* debug control */
    m2sio_dt,           /* debug flags */
    NULL,               /* mem size routine */
    NULL,               /* logical name */
    &m2sio_show_help,   /* help */
    NULL,               /* attach help */
    NULL,               /* context for help */
    &m2sio_description  /* description */
};

DEVICE m2sio1_dev = {
    M2SIO1_SNAME,       /* name */
    unit1,        /* unit */
    reg1,         /* registers */
    m2sio_mod,          /* modifiers */
    1,                  /* # units */
    10,                 /* address radix */
    31,                 /* address width */
    1,                  /* address increment */
    8,                  /* data radix */
    8,                  /* data width */
    NULL,               /* examine routine */
    NULL,               /* deposit routine */
    &m2sio1_reset,      /* reset routine */
    NULL,               /* boot routine */
    &m2sio_attach,      /* attach routine */
    &m2sio_detach,      /* detach routine */
    &m2sio1_res,        /* context */
    (DEV_DISABLE | DEV_DEBUG | DEV_MUX),  /* flags */
    0,                  /* debug control */
    m2sio_dt,           /* debug flags */
    NULL,               /* mem size routine */
    NULL,               /* logical name */
    &m2sio_show_help,   /* help */
    NULL,               /* attach help */
    NULL,               /* context for help */
    &m2sio_description  /* description */
};

static const char* m2sio_description(DEVICE *dptr)
{
    return M2SIO_NAME;
}

static t_stat m2sio0_reset(DEVICE *dptr)
{
    dptr->units->up8 = &m2sio0_reg;

    return(m2sio_reset(dptr, &m2sio0_io));
}

static t_stat m2sio1_reset(DEVICE *dptr)
{
    dptr->units->up8 = &m2sio1_reg;

    return(m2sio_reset(dptr, &m2sio1_io));
}

static t_stat m2sio_reset(DEVICE *dptr, int32 (*routine)(const int32, const int32, const int32))
{
    RES *res;
    M2SIO_REG *reg;

    if ((res = (RES *) dptr->ctxt) == NULL) {
        return SCPE_IERR;
    }
    if ((reg = (M2SIO_REG *) dptr->units->up8) == NULL) {
        return SCPE_IERR;
    }

    /* Connect/Disconnect I/O Ports at base address */
    if (dptr->flags & DEV_DIS) { /* Device is disabled */
        s100_bus_remio(res->io_base, res->io_size, routine);
        s100_bus_noconsole(&dptr->units[0]);

        return SCPE_OK;
    }

    /* Device is enabled */
    s100_bus_addio(res->io_base, res->io_size, routine, dptr->name);

    /* Set as CONSOLE unit  */
    if (dptr->units[0].flags & UNIT_M2SIO_CONSOLE) {
        s100_bus_console(&dptr->units[0]);
    }

    /* Set DEVICE for this UNIT */
    dptr->units[0].dptr = dptr;
    dptr->units[0].wait = M2SIO_WAIT;

    /* Enable TMXR modem control passthrough */
    tmxr_set_modem_control_passthru(res->tmxr);

    /* Reset status registers */
    reg->stb = M2SIO_CTS | M2SIO_DCD;
    reg->txp = FALSE;
    reg->dcdl = FALSE;

    if (dptr->units[0].flags & UNIT_ATT) {
        m2sio_config_rts(dptr, 1);    /* disable RTS */
    }

    /* Start service routine */
    sim_activate(&dptr->units[0], dptr->units[0].wait);

    sim_debug(STATUS_MSG, dptr, "reset adapter.\n");

    return SCPE_OK;
}


static t_stat m2sio_svc(UNIT *uptr)
{
    DEVICE *dptr;
    RES *res;
    M2SIO_REG *reg;
    int32 c,s,stb;
    t_stat r;

    if ((dptr = find_dev_from_unit(uptr)) == NULL)
        return SCPE_IERR;

    if ((res = (RES *) dptr->ctxt) == NULL) {
        return SCPE_IERR;
    }
    if ((reg = (M2SIO_REG *) uptr->up8) == NULL) {
        return SCPE_IERR;
    }

    /* Check for new incoming connection */
    if (uptr->flags & UNIT_ATT) {
        if (tmxr_poll_conn(res->tmxr) >= 0) {      /* poll connection */

            reg->conn = TRUE;          /* set connected   */

            sim_debug(STATUS_MSG, uptr->dptr, "new connection.\n");
        }
    }

    /* Update incoming modem status bits */
    if (uptr->flags & UNIT_ATT) {
        tmxr_set_get_modem_bits(res->tmxr->ldsc, 0, 0, &s);
        stb = reg->stb;
        reg->stb &= ~M2SIO_CTS;
        reg->stb |= ((s & TMXR_MDM_CTS) || (uptr->flags & UNIT_M2SIO_CTS)) ? 0 : M2SIO_CTS;     /* Active Low */
        if ((stb ^ reg->stb) & M2SIO_CTS) {
            sim_debug(STATUS_MSG, uptr->dptr, "CTS state changed to %s.\n", (reg->stb & M2SIO_CTS) ? "LOW" : "HIGH");
        }

        if (!reg->dcdl) {
            reg->stb &= ~M2SIO_DCD;
            reg->stb |= ((s & TMXR_MDM_DCD) || (uptr->flags & UNIT_M2SIO_DCD)) ? 0 : M2SIO_DCD;     /* Active Low */
            if ((stb ^ reg->stb) & M2SIO_DCD) {
                if ((reg->stb & M2SIO_DCD) == M2SIO_DCD) {
                    reg->dcdl = TRUE;
                    if (reg->rie) {
                        m2sio_int(uptr);
                    }
                }
                sim_debug(STATUS_MSG, uptr->dptr, "DCD state changed to %s.\n", (reg->stb & M2SIO_DCD) ? "LOW" : "HIGH");
            }
        }

        /* Enable receiver if DCD is active low */
        res->tmxr->ldsc->rcve = !(reg->stb & M2SIO_DCD);
    }

    /* TX data */
    if (reg->txp) {
        if (uptr->flags & UNIT_ATT) {
            if (!(reg->stb & M2SIO_CTS)) {    /* Active low */
                r = tmxr_putc_ln(res->tmxr->ldsc, reg->txb);
                reg->txp = FALSE;             /* Reset TX Pending */
            } else {
                r = SCPE_STALL;
            }
        } else {
            r = sim_putchar(reg->txb);
            reg->txp = FALSE;                 /* Reset TX Pending */
        }

        if (r == SCPE_LOST) {
            reg->conn = FALSE;          /* Connection was lost */
            sim_debug(STATUS_MSG, uptr->dptr, "lost connection.\n");
        }

        /* If TX buffer now empty, send interrupt */
        if ((!reg->txp) && (reg->tie)) {
            m2sio_int(uptr);
        }

    }

    /* Update TDRE if not set and no character pending */
    if (!reg->txp && !(reg->stb & M2SIO_TDRE)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_tx(res->tmxr);
            reg->stb |= (tmxr_txdone_ln(res->tmxr->ldsc) && reg->conn) ? M2SIO_TDRE : 0;
        } else {
            reg->stb |= M2SIO_TDRE;
        }
    }

    /* Check for Data if RX buffer empty */
    if (!(reg->stb & M2SIO_RDRF)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_rx(res->tmxr);

            c = tmxr_getc_ln(res->tmxr->ldsc);
        } else {
            c = s100_bus_poll_kbd(uptr);
        }

        if (c & (TMXR_VALID | SCPE_KFLAG)) {
            reg->rxb = m2sio_map_kbdchar(uptr, c);
            reg->stb |= M2SIO_RDRF;
            reg->stb &= ~(M2SIO_FE | M2SIO_OVRN | M2SIO_PE);
            if (reg->rie) {
                m2sio_int(uptr);
            }
        }
    }

    sim_activate_abs(uptr, uptr->wait);

    return SCPE_OK;
}


/* Attach routine */
static t_stat m2sio_attach(UNIT *uptr, const char *cptr)
{
    DEVICE *dptr;
    RES *res;
    M2SIO_REG *reg;
    t_stat r;

    if ((dptr = find_dev_from_unit(uptr)) == NULL)
        return SCPE_IERR;

    if ((res = (RES *) dptr->ctxt) == NULL) {
        return SCPE_IERR;
    }
    if ((reg = (M2SIO_REG *) uptr->up8) == NULL) {
        return SCPE_IERR;
    }

    sim_debug(VERBOSE_MSG, uptr->dptr, "attach (%s).\n", cptr);

    if ((r = tmxr_attach(res->tmxr, uptr, cptr)) == SCPE_OK) {

        if (res->tmxr->ldsc->serport) {
            r = m2sio_config_rts(uptr->dptr, reg->rts);    /* update RTS */
        }

        res->tmxr->ldsc->rcve = 1;
    }

    return r;
}


/* Detach routine */
static t_stat m2sio_detach(UNIT *uptr)
{
    DEVICE *dptr;
    RES *res;

    if ((dptr = find_dev_from_unit(uptr)) == NULL)
        return SCPE_IERR;

    if ((res = (RES *) dptr->ctxt) == NULL) {
        return SCPE_IERR;
    }

    sim_debug(VERBOSE_MSG, uptr->dptr, "detach.\n");

    if (uptr->flags & UNIT_ATT) {
        sim_cancel(uptr);

        return (tmxr_detach(res->tmxr, uptr));
    }

    return SCPE_UNATT;
}

static t_stat m2sio_set_console(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    if (value == UNIT_M2SIO_CONSOLE) {
        s100_bus_console(uptr);
    }
    else {
        s100_bus_noconsole(uptr);
    }

    return SCPE_OK;
}

static t_stat m2sio_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    M2SIO_REG *reg;
    int32 baud;
    t_stat r = SCPE_ARG;

    if ((reg = (M2SIO_REG *) uptr->up8) == NULL) {
        return SCPE_IERR;
    }

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
                case 19200:
                    reg->baud = baud;
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
    M2SIO_REG *reg;

    if ((reg = (M2SIO_REG *) uptr->up8) == NULL) {
        return SCPE_IERR;
    }

    if (uptr->flags & UNIT_ATT) {
        fprintf(st, "Baud rate: %d", reg->baud);
    }

    return SCPE_OK;
}

static t_stat m2sio_config_line(UNIT *uptr)
{
    DEVICE *dptr;
    RES *res;
    M2SIO_REG *reg;
    char config[20];
    const char *fmt;
    t_stat r = SCPE_IERR;

    if ((dptr = find_dev_from_unit(uptr)) == NULL)
        return SCPE_IERR;

    if ((res = (RES *) dptr->ctxt) == NULL) {
        return SCPE_IERR;
    }
    if ((reg = (M2SIO_REG *) uptr->up8) == NULL) {
        return SCPE_IERR;
    }

    if (reg != NULL) {
        switch (reg->ctb & M2SIO_FMTMSK) {
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

        sprintf(config, "%d-%s", reg->baud, fmt);

        r = tmxr_set_config_line(res->tmxr->ldsc, config);

        sim_debug(STATUS_MSG, uptr->dptr, "port configuration set to '%s'.\n", config);
    }

    return r;
}

/*
** RTS is active low
** 0 = RTS active
** 1 = RTS inactive
*/
static t_stat m2sio_config_rts(DEVICE *dptr, char rts)
{
    RES *res;
    M2SIO_REG *reg;
    t_stat r = SCPE_OK;
    int32 s;

    if ((res = (RES *) dptr->ctxt) == NULL) {
        return SCPE_IERR;
    }
    if ((reg = (M2SIO_REG *) dptr->units->up8) == NULL) {
        return SCPE_IERR;
    }

    if (dptr->units[0].flags & UNIT_ATT) {
        /* RTS Control */
        s = TMXR_MDM_RTS;
        if (dptr->units[0].flags & UNIT_M2SIO_DTR) {
            s |= TMXR_MDM_DTR;
        }

        if (!rts) {
            r = tmxr_set_get_modem_bits(res->tmxr->ldsc, s, 0, NULL);
            if (reg->rts) {
                sim_debug(STATUS_MSG, dptr, "RTS state changed to HIGH.\n");
            }
        } else {
            r = tmxr_set_get_modem_bits(res->tmxr->ldsc, 0, s, NULL);
            if (!reg->rts) {
                sim_debug(STATUS_MSG, dptr, "RTS state changed to LOW.\n");
            }
        }
    }

    reg->rts = rts;    /* Active low */

    return r;
}

static int32 m2sio0_io(int32 addr, int32 io, int32 data)
{
    return(m2sio_io(&m2sio0_dev, addr, io, data));
}

static int32 m2sio1_io(int32 addr, int32 io, int32 data)
{
    return(m2sio_io(&m2sio1_dev, addr, io, data));
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
    M2SIO_REG *reg;
    int32 r;

    if ((reg = (M2SIO_REG *) dptr->units->up8) == NULL) {
        return SCPE_IERR;
    }

    if (io == S100_IO_READ) {
        r = reg->stb;
    } else {
        reg->ctb = data & 0xff;    /* save control byte */

        /* Master Reset */
        if ((data & M2SIO_RESET) == M2SIO_RESET) {
            sim_debug(STATUS_MSG, dptr, "MC6850 master reset.\n");
            reg->stb &= (M2SIO_CTS | M2SIO_DCD);           /* Reset status register */
            reg->rxb = 0x00;
            reg->txp = FALSE;
            reg->tie = FALSE;
            reg->rie = FALSE;
            reg->dcdl = FALSE;
            m2sio_config_rts(dptr, 1);    /* disable RTS */
        } else {
            /* Interrupt Enable */
            reg->rie = (data & M2SIO_RIE) == M2SIO_RIE;           /* Receive interrupt enable  */
            reg->tie = (data & M2SIO_RTSMSK) == M2SIO_RTSLTIE;    /* Transmit interrupt enable */
            switch (data & M2SIO_RTSMSK) {
                case M2SIO_RTSLTIE:
                case M2SIO_RTSLTID:
                    m2sio_config_rts(dptr, 0);    /* enable RTS */
                    break;

                case M2SIO_RTSHTID:
                case M2SIO_RTSHTBR:
                    m2sio_config_rts(dptr, 1);    /* disable RTS */
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
    M2SIO_REG *reg;
    int32 r;

    if ((reg = (M2SIO_REG *) dptr->units->up8) == NULL) {
        return SCPE_IERR;
    }

    if (io == S100_IO_READ) {
        r = reg->rxb;
        reg->stb &= ~(M2SIO_RDRF | M2SIO_FE | M2SIO_OVRN | M2SIO_PE | M2SIO_IRQ);
        reg->dcdl = FALSE;
    } else {
        reg->txb = data;
        reg->stb &= ~(M2SIO_TDRE | M2SIO_IRQ);
        reg->txp = TRUE;
        r = 0x00;
    }

    return r;
}

static void m2sio_int(UNIT *uptr)
{
    M2SIO_REG *reg;

    if ((reg = (M2SIO_REG *) uptr->up8) == NULL) {
        return;
    }

    if (reg->intenable) {
        s100_bus_int((1 << reg->intvector), reg->databus);  /* Generate interrupt on the bus */
        reg->stb |= M2SIO_IRQ;

        sim_debug(IRQ_MSG, uptr->dptr, "%s: IRQ Vector=%d Status=%02X\n", sim_uname(uptr), reg->intvector, reg->stb);
    }
}

static int32 m2sio_map_kbdchar(UNIT *uptr, int32 ch)
{
    ch &= 0xff;

    if (uptr->flags & UNIT_M2SIO_MAP) {
        if (uptr->flags & UNIT_M2SIO_BS) {
            if (ch == KBD_BS) {
                return KBD_DEL;
            }
        }
        else if (ch == KBD_DEL) {
            return KBD_BS;
        }

        if (uptr->flags & UNIT_M2SIO_UPPER) {
            return toupper(ch);
        }
    }

    return ch;
}

static t_stat m2sio_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nAltair 8800 88-2SIO (%s)\n", dptr->name);

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);
    fprintf(st, "\n\n");
    tmxr_attach_help(st, dptr, uptr, flag, cptr);

    fprintf(st, "----- NOTES -----\n\n");
    fprintf(st, "Only one device may poll the host keyboard for CONSOLE input.\n");
    fprintf(st, "Use SET %s CONSOLE to select this UNIT as the CONSOLE device.\n", sim_dname(dptr));
    fprintf(st, "\nUse SHOW BUS CONSOLE to display the current CONSOLE device.\n\n");

    return SCPE_OK;
}

