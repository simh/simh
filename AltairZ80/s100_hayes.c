/* s100_hayes: D.C. Hayes 80-103A and Micromodem-100

   Copyright (c) 2020, Patrick Linstruth <patrick@deltecent.com>

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


   This device emulates D.C. Hayes 80-103A and Micromodem 100 communications
   adapters.

   To provide any useful funcationality, this device need to be attached to
   a socket or serial port. Enter "HELP HAYES" at the "simh>" prompt for
   additional information.
*/

#include <stdio.h>

#include "altairz80_defs.h"
#include "sim_tmxr.h"

#define HAYES_NAME  "HAYES MODEM"
#define HAYES_SNAME "HAYES"

#define HAYES_WAIT        500            /* Service Wait Interval */

#define HAYES_IOBASE      0x80
#define HAYES_IOSIZE      4

#define HAYES_REG0        0              /* Relative Address 0 */
#define HAYES_REG1        1              /* Relative Address 1 */
#define HAYES_REG2        2              /* Relative Address 2 */
#define HAYES_REG3        3              /* Relative Address 3 */

#define HAYES_RRF         0x01           /* Receive Data Register Full   */
#define HAYES_TRE         0x02           /* Transmit Data Register Empty */
#define HAYES_PE          0x04           /* Parity Error                 */
#define HAYES_FE          0x08           /* Framing Error                */
#define HAYES_OE          0x10           /* Overrun                      */
#define HAYES_TMR         0x20           /* Timer Status                 */
#define HAYES_CD          0x40           /* Overrun                      */
#define HAYES_RI          0x80           /* Not Ring Indicator           */

#define HAYES_BRS         0x01           /* Baud Rate Select             */
#define HAYES_TXE         0x02           /* Transmitter Enable           */
#define HAYES_ORIG        0x04           /* Mode Select                  */
#define HAYES_MS          0x04           /* Mode Select                  */
#define HAYES_BK          0x08           /* Exchange Mark & Space        */
#define HAYES_ST          0x10           /* Self Test Mode               */
#define HAYES_TIE         0x20           /* Transmit Interrupt Enable    */
#define HAYES_OH          0x80           /* Off Hook                     */

#define HAYES_5BIT        0x00           /* 5 Data Bits                  */
#define HAYES_6BIT        0x02           /* 6 Data Bits                  */
#define HAYES_7BIT        0x04           /* 7 Data Bits                  */
#define HAYES_8BIT        0x06           /* 8 Data Bits                  */
#define HAYES_BMSK        0x06           /* Data Bits Bit Mask           */

#define HAYES_OPAR        0x00           /* Odd Parity                   */
#define HAYES_EPAR        0x01           /* Odd Parity                   */
#define HAYES_PI          0x10           /* Parity Inhibit               */
#define HAYES_PMSK        0x11           /* Parity Bit Mask              */

#define HAYES_1SB         0x00           /* 1 Stop Bit                   */
#define HAYES_15SB        0x08           /* 1.5 Stop Bits                */
#define HAYES_2SB         0x08           /* 2 Stop Bits                  */
#define HAYES_SMSK        0x08           /* Stop Bits Bit Mask           */

#define HAYES_LMSK        0x1F           /* Line Bit Bask                */

#define HAYES_CLOCK       2500           /* Rate Generator / 100         */
#define HAYES_BAUD        300            /* Default baud rate            */

/* Debug flags */
#define STATUS_MSG        (1 << 0)
#define ERROR_MSG         (1 << 1)
#define VERBOSE_MSG       (1 << 2)
#define DEBUG_MSG         (1 << 3)

/* IO Read/Write */
#define IO_RD            0x00            /* IO Read  */
#define IO_WR            0x01            /* IO Write */

typedef struct {
    PNP_INFO pnp;        /* Must be first    */
    TMLN *tmln;          /* TMLN pointer     */
    TMXR *tmxr;          /* TMXR pointer     */
    int32 baud;          /* Baud rate        */
    int32 txp;           /* Transmit Pending */
    int32 dtr;           /* DTR Status       */
    int32 ireg0;         /* In Register 0    */
    int32 ireg1;         /* In Register 1    */
    int32 oreg0;         /* Out Register 0   */
    int32 oreg1;         /* Out Register 1   */
    int32 oreg2;         /* Out Register 2   */
    int32 oreg3;         /* Out Register 3   */
    int32 intmsk;        /* Interrupt Mask   */
    uint32 timer;        /* 50ms Timer       */
    uint32 flags;        /* Original Flags   */
} HAYES_CTX;

extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);


static const char* hayes_description(DEVICE *dptr);
static t_stat hayes_svc(UNIT *uptr);
static t_stat hayes_reset(DEVICE *dptr);
static t_stat hayes_attach(UNIT *uptr, CONST char *cptr);
static t_stat hayes_detach(UNIT *uptr);
static t_stat hayes_config_line(UNIT *uptr);
static t_stat hayes_set_dtr(UNIT *uptr, int32 flag);
static int32 hayes_io(int32 addr, int32 io, int32 data);
static int32 hayes_reg0(int32 io, int32 data);
static int32 hayes_reg1(int32 io, int32 data);
static int32 hayes_reg2(int32 io, int32 data);
static int32 hayes_reg3(int32 io, int32 data);

/* Debug Flags */
static DEBTAB hayes_dt[] = {
    { "STATUS",         STATUS_MSG,         "Status messages"  },
    { "ERROR",          ERROR_MSG,          "Error messages"  },
    { "VERBOSE",        VERBOSE_MSG,        "Verbose messages"  },
    { "DEBUG",          DEBUG_MSG,          "Debug messages"  },
    { NULL,             0                   }
};

/* Terminal multiplexer library descriptors */

static TMLN hayes_tmln[] = {         /* line descriptors */
    { 0 }
};


static TMXR hayes_tmxr = {                      /* multiplexer descriptor */
    1,                                          /* number of terminal lines */
    0,                                          /* listening port (reserved) */
    0,                                          /* master socket  (reserved) */
    hayes_tmln,                                 /* line descriptor array */
    NULL,                                       /* line connection order */
    NULL                                        /* multiplexer device (derived internally) */
};

static MTAB hayes_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets MITS 2SIO base I/O address"   },
    { 0 }
};

static HAYES_CTX hayes_ctx = {{0, 0, HAYES_IOBASE, HAYES_IOSIZE}, hayes_tmln, &hayes_tmxr, HAYES_BAUD, 1};

static UNIT hayes_unit[] = {
        { UDATA (&hayes_svc, UNIT_ATTABLE | UNIT_DISABLE, 0), HAYES_WAIT },
};

static REG hayes_reg[] = {
    { HRDATAD (IREG0, hayes_ctx.ireg0, 8, "HAYES input register 0"), },
    { HRDATAD (IREG1, hayes_ctx.ireg1, 8, "HAYES input register 1"), },
    { HRDATAD (OREG0, hayes_ctx.oreg0, 8, "HAYES output register 0"), },
    { HRDATAD (OREG1, hayes_ctx.oreg1, 8, "HAYES output register 1"), },
    { HRDATAD (OREG2, hayes_ctx.oreg2, 8, "HAYES output register 2"), },
    { HRDATAD (OREG3, hayes_ctx.oreg3, 8, "HAYES output register 3"), },
    { HRDATAD (TXP, hayes_ctx.txp, 8, "HAYES TX data pending"), },
    { HRDATAD (DTR, hayes_ctx.dtr, 8, "HAYES DTR status"), },
    { DRDATAD (BAUD, hayes_ctx.baud, 8, "HAYES baud rate"), },
    { HRDATAD (INTMSK, hayes_ctx.intmsk, 8, "HAYES interrupt mask"), },
    { FLDATAD (RRF, hayes_ctx.ireg1, 0, "HAYES RRF status"), },
    { FLDATAD (TRE, hayes_ctx.ireg1, 1, "HAYES TRE status"), },
    { FLDATAD (PE, hayes_ctx.ireg1, 2, "HAYES PE status"), },
    { FLDATAD (FE, hayes_ctx.ireg1, 3, "HAYES FE status"), },
    { FLDATAD (OE, hayes_ctx.ireg1, 4, "HAYES OE status"), },
    { FLDATAD (TMR, hayes_ctx.ireg1, 5, "HAYES TMR status"), },
    { FLDATAD (CD, hayes_ctx.ireg1, 6, "HAYES CD status"), },
    { FLDATAD (RI, hayes_ctx.ireg1, 7, "HAYES NOT RINGING status"), },
    { FLDATAD (TXE, hayes_ctx.oreg2, 1, "HAYES TXE status"), },
    { FLDATAD (ST, hayes_ctx.oreg2, 4, "HAYES ST status"), },
    { FLDATAD (OH, hayes_ctx.oreg2, 7, "HAYES OH status"), },
    { DRDATAD (TIMER, hayes_ctx.timer, 32, "Hayes timer ms"), },
    { DRDATAD (WAIT, hayes_unit[0].wait, 32, "HAYES wait cycles"), },
    { NULL }
};

DEVICE hayes_dev = {
    HAYES_SNAME,  /* name */
    hayes_unit,   /* unit */
    hayes_reg,    /* registers */
    hayes_mod,    /* modifiers */
    1,            /* # units */
    10,           /* address radix */
    31,           /* address width */
    1,            /* address increment */
    8,            /* data radix */
    8,            /* data width */
    NULL,         /* examine routine */
    NULL,         /* deposit routine */
    &hayes_reset,  /* reset routine */
    NULL,         /* boot routine */
    &hayes_attach,         /* attach routine */
    &hayes_detach,         /* detach routine */
    &hayes_ctx,           /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX),  /* flags */
    0,                            /* debug control */
    hayes_dt,                           /* debug flags */
    NULL,                                 /* mem size routine */
    NULL,                                 /* logical name */
    NULL,                                 /* help */
    NULL,                                 /* attach help */
    NULL,                                 /* context for help */
    &hayes_description                    /* description */
};

static const char* hayes_description(DEVICE *dptr)
{
    return HAYES_NAME;
}

static t_stat hayes_reset(DEVICE *dptr)
{
    /* Connect/Disconnect I/O Ports at base address */
    if (sim_map_resource(hayes_ctx.pnp.io_base, hayes_ctx.pnp.io_size, RESOURCE_TYPE_IO, &hayes_io, dptr->name, dptr->flags & DEV_DIS) != 0) {
        sim_debug(ERROR_MSG, dptr, "error mapping I/O resource at 0x%02x.\n", hayes_ctx.pnp.io_base);
        return SCPE_ARG;
    }

    /* Set DEVICE for this UNIT */
    dptr->units[0].dptr = dptr;

    /* Enable TMXR modem control passthru */
    tmxr_set_modem_control_passthru(hayes_ctx.tmxr);

    /* Reset status registers */
    hayes_ctx.ireg0 = 0;
    hayes_ctx.ireg1 = HAYES_RI;
    hayes_ctx.oreg1 = HAYES_8BIT | HAYES_PI;
    hayes_ctx.oreg2 = 0;
    hayes_ctx.oreg3 = 0;
    hayes_ctx.txp = 0;
    hayes_ctx.dtr = 0;
    hayes_ctx.intmsk = 0;
    hayes_ctx.timer = 0;
    hayes_ctx.baud = HAYES_BAUD;

    if (!(dptr->flags & DEV_DIS)) {
        sim_activate(&dptr->units[0], dptr->units[0].wait);
    } else {
        sim_cancel(&dptr->units[0]);
    }

    sim_debug(STATUS_MSG, dptr, "reset adapter.\n");

    return SCPE_OK;
}

static t_stat hayes_svc(UNIT *uptr)
{
    int32 c,s,ireg1;
    t_stat r;
    uint32 ms;

    /* Check for new incoming connection */
    if (uptr->flags & UNIT_ATT) {
        if (tmxr_poll_conn(hayes_ctx.tmxr) >= 0) {      /* poll connection */
            sim_debug(STATUS_MSG, uptr->dptr, "new connection.\n");
        }
    }

    /* Update incoming modem status bits */
    if (uptr->flags & UNIT_ATT) {
        tmxr_set_get_modem_bits(hayes_ctx.tmln, 0, 0, &s);

        ireg1 = hayes_ctx.ireg1;

        hayes_ctx.ireg1 &= ~HAYES_RI;
        hayes_ctx.ireg1 |= (s & TMXR_MDM_RNG) ? 0 : HAYES_RI;     /* Active Low */

        /* RI status changed */
        if ((ireg1 ^ hayes_ctx.ireg1) & HAYES_RI) {
            sim_debug(STATUS_MSG, uptr->dptr, "RI state changed to %s.\n", (hayes_ctx.ireg1 & HAYES_RI) ? "LOW" : "HIGH");

            /*
            ** The Hayes does not have DTR or RTS control signals.
            ** TMXR will not accept a socket connection unless DTR
            ** is active and there is no way to tell TMXR to ignore
            ** them, so we raise DTR here on RI.
            */
            if (!(hayes_ctx.ireg1 & HAYES_RI)) {    /* RI is active low */
                hayes_set_dtr(uptr, 1);
            }
        }

        hayes_ctx.ireg1 &= ~HAYES_CD;
        hayes_ctx.ireg1 |= (s & TMXR_MDM_DCD) ? HAYES_CD : 0;    /* Active High */

        /* CD status changed */
        if ((ireg1 ^ hayes_ctx.ireg1) & HAYES_CD) {
            sim_debug(STATUS_MSG, uptr->dptr, "CD state changed to %s.\n", (hayes_ctx.ireg1 & HAYES_CD) ? "HIGH" : "LOW");

            /*
            ** The Hayes does not have DTR or RTS control signals.
            ** TMXR will not maintain a socket connection unless DTR
            ** is active and there is no way to tell TMXR to
            ** ignore them, so we drop DTR here on loss of CD.
            */
            if (!(hayes_ctx.ireg1 & HAYES_CD)) {
                hayes_set_dtr(uptr, 0);
            }
        }
    }

    /* TX data */
    if (hayes_ctx.txp && hayes_ctx.oreg2 & HAYES_TXE) {
        if (uptr->flags & UNIT_ATT) {
            r = tmxr_putc_ln(hayes_ctx.tmln, hayes_ctx.oreg0);
        } else {
            r = sim_putchar(hayes_ctx.oreg0);
        }

        hayes_ctx.txp = 0;               /* Reset TX Pending */

        if (r == SCPE_LOST) {
            sim_debug(STATUS_MSG, uptr->dptr, "lost connection.\n");
        }
    }

    /* Update TRE if not set and no character pending */
    if (!hayes_ctx.txp && !(hayes_ctx.ireg1 & HAYES_TRE)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_tx(hayes_ctx.tmxr);
            hayes_ctx.ireg1 |= (tmxr_txdone_ln(hayes_ctx.tmln)) ? HAYES_TRE : 0;
        } else {
            hayes_ctx.ireg1 |= HAYES_TRE;
        }
    }

    /* Check for Data if RX buffer empty */
    if (!(hayes_ctx.ireg1 & HAYES_RRF)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_rx(hayes_ctx.tmxr);

            c = tmxr_getc_ln(hayes_ctx.tmln);
        } else {
            c = sim_poll_kbd();
        }

        if (c & (TMXR_VALID | SCPE_KFLAG)) {
            hayes_ctx.ireg0 = c & 0xff;
            hayes_ctx.ireg1 |= HAYES_RRF;
            hayes_ctx.ireg1 &= ~(HAYES_FE | HAYES_OE | HAYES_PE);
        }
    }

    /* Timer */
    ms = sim_os_msec();

    if (hayes_ctx.timer && ms > hayes_ctx.timer) {
        if (!(hayes_ctx.ireg1 & HAYES_TMR)) {
            sim_debug(VERBOSE_MSG, uptr->dptr, "50ms timer triggered.\n");
        }

        hayes_ctx.ireg1 |= HAYES_TMR;
    }

    /* Don't let TMXR clobber our wait time */
    uptr->wait = HAYES_WAIT;

    sim_activate_abs(uptr, uptr->wait);

    return SCPE_OK;
}


/* Attach routine */
static t_stat hayes_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    sim_debug(VERBOSE_MSG, uptr->dptr, "attach (%s).\n", cptr);

    if ((r = tmxr_attach(hayes_ctx.tmxr, uptr, cptr)) == SCPE_OK) {

        hayes_ctx.flags = uptr->flags;     /* Save Flags */

        hayes_ctx.tmln->rcve = 1;

        hayes_config_line(uptr);

        /*
        ** The Hayes does not have DTR or RTS control signals.
        ** We raise RTS here for use to provide DCD/RI signals.
        ** We drop DTR as that is tied to the other functions.
        */
        tmxr_set_get_modem_bits(hayes_ctx.tmln, TMXR_MDM_RTS, TMXR_MDM_DTR, NULL);
        hayes_ctx.dtr = 0;
        sim_debug(STATUS_MSG, uptr->dptr, "Raising RTS. Dropping DTR.\n");

        sim_activate(uptr, uptr->wait);

        sim_debug(VERBOSE_MSG, uptr->dptr, "activated service.\n");
    }

    return r;
}


/* Detach routine */
static t_stat hayes_detach(UNIT *uptr)
{
    sim_debug(VERBOSE_MSG, uptr->dptr, "detach.\n");

    if (uptr->flags & UNIT_ATT) {
        uptr->flags = hayes_ctx.flags;     /* Restore Flags */

        sim_cancel(uptr);

        return (tmxr_detach(hayes_ctx.tmxr, uptr));
    }

    return SCPE_UNATT;
}

static t_stat hayes_config_line(UNIT *uptr)
{
    char config[20];
    char b,p,s;
    t_stat r = SCPE_IERR;

    switch (hayes_ctx.oreg1 & HAYES_BMSK) {
        case HAYES_5BIT:
            b = '5';
            break;

        case HAYES_6BIT:
            b = '6';
            break;

        case HAYES_7BIT:
            b = '7';
            break;

        case HAYES_8BIT:
        default:
            b = '8';
            break;
    }

    switch (hayes_ctx.oreg1 & HAYES_PMSK) {
        case HAYES_OPAR:
            p = 'O';
            break;

        case HAYES_EPAR:
            p = 'E';
            break;

        default:
            p = 'N';
            break;
    }

    switch (hayes_ctx.oreg1 & HAYES_SMSK) {
        case HAYES_2SB:
            s = '2';
            break;

        case HAYES_1SB:
        default:
            s = '1';
            break;
    }

    sprintf(config, "%d-%c%c%c", hayes_ctx.baud, b,p,s);

    r = tmxr_set_config_line(hayes_ctx.tmln, config);

    if (r) {
        sim_debug(ERROR_MSG, uptr->dptr, "error %d setting port configuration to '%s'.\n", r, config);
    } else {
        sim_debug(STATUS_MSG, uptr->dptr, "port configuration set to '%s'.\n", config);
    }

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
    hayes_ctx.tmln->txbps = 0;   /* Get TMXR's rate-limiting out of our way */
    hayes_ctx.tmln->rxbps = 0;   /* Get TMXR's rate-limiting out of our way */

    return r;
}

static t_stat hayes_set_dtr(UNIT *uptr, int32 flag)
{
    t_stat r = SCPE_IERR;

    if (hayes_ctx.dtr && !flag) {
        r = tmxr_set_get_modem_bits(hayes_ctx.tmln, 0, TMXR_MDM_DTR, NULL);
        sim_debug(STATUS_MSG, uptr->dptr, "Dropping DTR.\n");
    } else if (!hayes_ctx.dtr && flag) {
        r = tmxr_set_get_modem_bits(hayes_ctx.tmln, TMXR_MDM_DTR, 0, NULL);
        sim_debug(STATUS_MSG, uptr->dptr, "Raising DTR.\n");
    }

    hayes_ctx.dtr = flag;

    return r;
}

static int32 hayes_io(int32 addr, int32 io, int32 data)
{
    int32 r = 0;

    addr &= 0xff;
    data &= 0xff;

    if (io == IO_WR) {
       sim_debug(VERBOSE_MSG, &hayes_dev, "OUT %02X,%02X\n", addr , data);
    } else {
       sim_debug(VERBOSE_MSG, &hayes_dev, "IN %02X\n", addr);
    }

    switch (addr & 0x03) {
        case HAYES_REG0:
            r = hayes_reg0(io, data);
            break;

        case HAYES_REG1:
            r = hayes_reg1(io, data);
            break;

        case HAYES_REG2:
            r = hayes_reg2(io, data);
            break;

        case HAYES_REG3:
            r = hayes_reg3(io, data);
            break;
    }

    return(r);
}

/*
** Register 0
**
** Input: Data
** Output: Data
*/
static int32 hayes_reg0(int32 io, int32 data)
{
    int32 r;

    if (io == IO_RD) {
        r = hayes_ctx.ireg0;
        hayes_ctx.ireg1 &= ~(HAYES_RRF);
    } else {
        hayes_ctx.oreg0 = data;
        hayes_ctx.ireg1 &= ~(HAYES_TRE);
        hayes_ctx.txp = 1;

        r = 0x00;
    }

    return(r);
}

/*
** Register 1
**
** Input: RI,CD,X,OE,FE,PE,TRE,RRF
** Output: X,X,X,PI,SBS,LS2,LS1,EPE
*/
static int32 hayes_reg1(int32 io, int32 data)
{
    int32 r;

    if (io == IO_RD) {
        r = hayes_ctx.ireg1;
        hayes_ctx.ireg1 &= ~(HAYES_FE | HAYES_OE | HAYES_PE);
    } else {
        hayes_ctx.oreg1 = data; /* Set UART configuration */

        hayes_config_line(&hayes_dev.units[0]);

        r = 0x00;
    }

    return(r);
}

/*
** Register 2
**
** Input: N/A
** Output: OH,X,TIE,ST,BK,MS,TXE,BRS
*/
static int32 hayes_reg2(int32 io, int32 data)
{
    int32 oreg2;

    if (io == IO_WR) {
        oreg2 = hayes_ctx.oreg2;   /* Save previous value */
        hayes_ctx.oreg2 = data;    /* Set new value */

        sim_debug(DEBUG_MSG, &hayes_dev, "oreg2 %02X -> %02X\n", oreg2, data);

        if (((oreg2 ^ data) & HAYES_OH)) {  /* Did OH status change? */

            sim_debug(STATUS_MSG, &hayes_dev, "Going %s hook.\n", (data & HAYES_OH) ? "OFF" : "ON");

            if (!(data & HAYES_OH)) {     /* Drop DTR if going ON HOOK */
                hayes_set_dtr(&hayes_dev.units[0], 0);
            }
        }

        /* Raise DTR if ORIGINATE and OFF HOOK */
        if (((oreg2 ^ data) & (HAYES_ORIG | HAYES_OH))) {  /* Did MODE or OH status change? */
            if ((data & HAYES_ORIG) && (data & HAYES_OH)) {
                hayes_set_dtr(&hayes_dev.units[0], 1);
            }
        }

        /* Did the line configuration change? */
        if ((oreg2 & HAYES_LMSK) != (data & HAYES_LMSK)) {
            hayes_ctx.baud = (data & HAYES_BRS) ? 300 : 110;

            hayes_config_line(&hayes_dev.units[0]);
        }
    }

    return(0x00);
}

/*
** Register 3
**
** Input: N/A
** Output: N/A
*/
static int32 hayes_reg3(int32 io, int32 data)
{
    if (io == IO_WR) {
        hayes_ctx.timer = sim_os_msec() + 50;    /* Set timeout to 50ms */
        hayes_ctx.ireg1 &= ~(HAYES_TMR);         /* Clear timer status */
    }

    return(0x00);
}

