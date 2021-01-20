/* s100_pmmi: PMMI MM-103 MODEM

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


   This device emulates a PMMI Communications MM-103 Modem & Communications
   adapter.

   The MM-103 uses 4 input and 4 output addresses. This driver defaults to
   E0-E3 hex.

   The MM-103 uses the Motorola MC6860L digital modem chip. This device does
   not have the ability to emulate the modulation and demodulation functions
   or the ability to connect to a phone line. All modem features, such as
   switch hook, dialtone detection, and dialing, are emulated in such a way
   that most software written for the MM-103 should function in some useful
   fashion.

   To provide any useful funcationality, this device need to be attached to
   a socket or serial port. Enter "HELP PMMI" at the "simh>" prompt for
   additional information.
*/

#include <stdio.h>

#include "altairz80_defs.h"
#include "sim_tmxr.h"

#define PMMI_NAME  "PMMI MM-103 MODEM"
#define PMMI_SNAME "PMMI"

#define PMMI_WAIT        500            /* Service Wait Interval */

#define PMMI_IOBASE      0xC0
#define PMMI_IOSIZE      4

#define PMMI_REG0        0              /* Relative Address 0 */
#define PMMI_REG1        1              /* Relative Address 1 */
#define PMMI_REG2        2              /* Relative Address 2 */
#define PMMI_REG3        3              /* Relative Address 3 */

#define PMMI_TBMT        0x01           /* Transmit Data Register Empty */
#define PMMI_DAV         0x02           /* Receive Data Register Full   */
#define PMMI_TEOC        0x04           /* Transmit Serializer Empty    */
#define PMMI_RPE         0x08           /* Parity Error                 */
#define PMMI_OR          0x10           /* Overrun                      */
#define PMMI_FE          0x20           /* Framing Error                */

#define PMMI_DT          0x01           /* Dial Tone                    */
#define PMMI_RNG         0x02           /* Ringing                      */
#define PMMI_CTS         0x04           /* Clear to Send                */
#define PMMI_RXBRK       0x08           /* RX Break                     */
#define PMMI_AP          0x10           /* Answer Phone                 */
#define PMMI_FO          0x20           /* Digital Carrier Signal       */
#define PMMI_MODE        0x40           /* Mode                         */
#define PMMI_TMR         0x80           /* Timer Pulses                 */

#define PMMI_ST          0x10           /* Self Test                    */
#define PMMI_DTR         0x40           /* DTR                          */

#define PMMI_SH          0x01           /* Switch Hook                  */
#define PMMI_RI          0x02           /* Ring Indicator               */
#define PMMI_5BIT        0x00           /* 5 Data Bits                  */
#define PMMI_6BIT        0x04           /* 6 Data Bits                  */
#define PMMI_7BIT        0x08           /* 7 Data Bits                  */
#define PMMI_8BIT        0x0C           /* 8 Data Bits                  */
#define PMMI_BMSK        0x0C           /* Data Bits Bit Mask           */

#define PMMI_OPAR        0x00           /* Odd Parity                   */
#define PMMI_NPAR        0x10           /* No Parity                    */
#define PMMI_EPAR        0x20           /* Odd Parity                   */
#define PMMI_PMSK        0x30           /* Parity Bit Mask              */

#define PMMI_1SB         0x00           /* 1 Stop Bit                   */
#define PMMI_15SB        0x40           /* 1.5 Stop Bits                */
#define PMMI_2SB         0x40           /* 2 Stop Bits                  */
#define PMMI_SMSK        0x40           /* Stop Bits Bit Mask           */

#define PMMI_CLOCK       2500           /* Rate Generator / 100         */
#define PMMI_BAUD        300            /* Default baud rate            */

/* Debug flags */
#define STATUS_MSG        (1 << 0)
#define ERROR_MSG         (1 << 1)
#define VERBOSE_MSG       (1 << 2)

/* IO Read/Write */
#define IO_RD            0x00            /* IO Read  */
#define IO_WR            0x01            /* IO Write */

typedef struct {
    PNP_INFO pnp;        /* Must be first    */
    int32 conn;          /* Connected Status */
    TMLN *tmln;          /* TMLN pointer     */
    TMXR *tmxr;          /* TMXR pointer     */
    int32 baud;          /* Baud rate        */
    int32 dtr;           /* DTR Status       */
    int32 txp;           /* Transmit Pending */
    int32 stb;           /* Status Buffer    */
    int32 ireg0;         /* In Register 0    */
    int32 ireg1;         /* In Register 1    */
    int32 ireg2;         /* In Register 2    */
    int32 ireg3;         /* In Register 3    */
    int32 oreg0;         /* Out Register 0   */
    int32 oreg1;         /* Out Register 1   */
    int32 oreg2;         /* Out Register 2   */
    int32 oreg3;         /* Out Register 3   */
    int32 intmsk;        /* Interrupt Mask   */
    uint32 ptimer;       /* Next Pulse Timer */
    uint32 dtimer;       /* Next DT Timer    */
    uint32 flags;        /* Original Flags   */
} PMMI_CTX;

extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);


static const char* pmmi_description(DEVICE *dptr);
static t_stat pmmi_svc(UNIT *uptr);
static t_stat pmmi_reset(DEVICE *dptr);
static t_stat pmmi_attach(UNIT *uptr, CONST char *cptr);
static t_stat pmmi_detach(UNIT *uptr);
static t_stat pmmi_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat pmmi_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc);
static t_stat pmmi_config_line(UNIT *uptr);
static int32 pmmi_io(int32 addr, int32 io, int32 data);
static int32 pmmi_reg0(int32 io, int32 data);
static int32 pmmi_reg1(int32 io, int32 data);
static int32 pmmi_reg2(int32 io, int32 data);
static int32 pmmi_reg3(int32 io, int32 data);

/* Debug Flags */
static DEBTAB pmmi_dt[] = {
    { "STATUS",         STATUS_MSG,         "Status messages"  },
    { "ERROR",          ERROR_MSG,          "Error messages"  },
    { "VERBOSE",        VERBOSE_MSG,        "Verbose messages"  },
    { NULL,             0                   }
};

/* Terminal multiplexer library descriptors */

static TMLN pmmi_tmln[] = {         /* line descriptors */
    { 0 }
};


static TMXR pmmi_tmxr = {                     /* multiplexer descriptor */
    1,                                          /* number of terminal lines */
    0,                                          /* listening port (reserved) */
    0,                                          /* master socket  (reserved) */
    pmmi_tmln,                                /* line descriptor array */
    NULL,                                       /* line connection order */
    NULL                                        /* multiplexer device (derived internally) */
};

#define UNIT_V_PMMI_RTS      (UNIT_V_UF + 0)     /* RTS follows DTR                */
#define UNIT_PMMI_RTS        (1 << UNIT_V_PMMI_RTS)

static MTAB pmmi_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets MITS 2SIO base I/O address"   },
    { UNIT_PMMI_RTS,       UNIT_PMMI_RTS,     "RTS",    "RTS",    NULL, NULL, NULL,
        "RTS follows DTR" },
    { UNIT_PMMI_RTS,       0,                  "NORTS",  "NORTS",  NULL, NULL, NULL,
        "RTS does not follow DTR (default)" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,  0,   "BAUD",  "BAUD",  &pmmi_set_baud, &pmmi_show_baud,
        NULL, "Set baud rate (default=300)" },
    { 0 }
};

static PMMI_CTX pmmi_ctx = {{0, 0, PMMI_IOBASE, PMMI_IOSIZE}, 0, pmmi_tmln, &pmmi_tmxr, PMMI_BAUD, 1};

static UNIT pmmi_unit[] = {
        { UDATA (&pmmi_svc, UNIT_ATTABLE | UNIT_DISABLE, 0), PMMI_WAIT },
};

static REG pmmi_reg[] = {
    { HRDATAD (IREG0, pmmi_ctx.ireg0, 8, "PMMI input register 0"), },
    { HRDATAD (IREG1, pmmi_ctx.ireg1, 8, "PMMI input register 1"), },
    { HRDATAD (IREG2, pmmi_ctx.ireg2, 8, "PMMI input register 2"), },
    { HRDATAD (IREG3, pmmi_ctx.ireg3, 8, "PMMI input register 3"), },
    { HRDATAD (OREG0, pmmi_ctx.oreg0, 8, "PMMI output register 0"), },
    { HRDATAD (OREG1, pmmi_ctx.oreg1, 8, "PMMI output register 1"), },
    { HRDATAD (OREG2, pmmi_ctx.oreg2, 8, "PMMI output register 2"), },
    { HRDATAD (OREG3, pmmi_ctx.oreg3, 8, "PMMI output register 3"), },
    { HRDATAD (TXP, pmmi_ctx.txp, 8, "PMMI tx data pending"), },
    { FLDATAD (CON, pmmi_ctx.conn, 0, "PMMI connection status"), },
    { DRDATAD (BAUD, pmmi_ctx.baud, 8, "PMMI calculated baud rate"), },
    { HRDATAD (INTMSK, pmmi_ctx.intmsk, 8, "PMMI interrupt mask"), },
    { FLDATAD (TBMT, pmmi_ctx.ireg0, 0, "PMMI TBMT status"), },
    { FLDATAD (DAV, pmmi_ctx.ireg0, 1, "PMMI DAV status"), },
    { FLDATAD (OR, pmmi_ctx.ireg0, 4, "PMMI OVRN status"), },
    { FLDATAD (DT, pmmi_ctx.ireg2, 0, "PMMI dial tone status (active low)"), },
    { FLDATAD (RNG, pmmi_ctx.ireg2, 1, "PMMI ringing status (active low)"), },
    { FLDATAD (CTS, pmmi_ctx.ireg2, 2, "PMMI CTS status (active low)"), },
    { FLDATAD (AP, pmmi_ctx.ireg2, 0, "PMMI answer phone status (active low)"), },
    { FLDATAD (PULSE, pmmi_ctx.ireg2, 7, "PMMI timer pulse"), },
    { DRDATAD (TIMER, pmmi_ctx.ptimer, 32, "PMMI timer pulse ms"), },
    { DRDATAD (WAIT, pmmi_unit[0].wait, 32, "PMMI wait cycles"), },
    { NULL }
};

DEVICE pmmi_dev = {
    PMMI_SNAME,  /* name */
    pmmi_unit,   /* unit */
    pmmi_reg,    /* registers */
    pmmi_mod,    /* modifiers */
    1,            /* # units */
    10,           /* address radix */
    31,           /* address width */
    1,            /* address increment */
    8,            /* data radix */
    8,            /* data width */
    NULL,         /* examine routine */
    NULL,         /* deposit routine */
    &pmmi_reset,  /* reset routine */
    NULL,         /* boot routine */
    &pmmi_attach,         /* attach routine */
    &pmmi_detach,         /* detach routine */
    &pmmi_ctx,           /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX),  /* flags */
    0,                            /* debug control */
    pmmi_dt,                           /* debug flags */
    NULL,                                 /* mem size routine */
    NULL,                                 /* logical name */
    NULL,                                 /* help */
    NULL,                                 /* attach help */
    NULL,                                 /* context for help */
    &pmmi_description                    /* description */
};

static const char* pmmi_description(DEVICE *dptr)
{
    return PMMI_NAME;
}

static t_stat pmmi_reset(DEVICE *dptr)
{
    /* Connect/Disconnect I/O Ports at base address */
    if (sim_map_resource(pmmi_ctx.pnp.io_base, pmmi_ctx.pnp.io_size, RESOURCE_TYPE_IO, &pmmi_io, dptr->name, dptr->flags & DEV_DIS) != 0) {
        sim_debug(ERROR_MSG, dptr, "error mapping I/O resource at 0x%02x.\n", pmmi_ctx.pnp.io_base);
        return SCPE_ARG;
    }

    /* Set DEVICE for this UNIT */
    dptr->units[0].dptr = dptr;

    /* Enable TMXR modem control passthru */
    tmxr_set_modem_control_passthru(pmmi_ctx.tmxr);

    /* Reset status registers */
    pmmi_ctx.ireg0 = 0;
    pmmi_ctx.ireg1 = 0;
    pmmi_ctx.ireg2 = PMMI_RNG | PMMI_CTS | PMMI_DT | PMMI_AP;
    pmmi_ctx.ireg3 = 0;
    pmmi_ctx.oreg0 = 0;
    pmmi_ctx.oreg1 = 0;
    pmmi_ctx.oreg2 = 0;
    pmmi_ctx.oreg3 = 0;
    pmmi_ctx.txp = 0;
    pmmi_ctx.intmsk = 0;
    pmmi_ctx.ptimer = sim_os_msec() + 40;
    pmmi_ctx.dtimer = 0;

    if (!(dptr->flags & DEV_DIS)) {
        sim_activate(&dptr->units[0], dptr->units[0].wait);
    } else {
        sim_cancel(&dptr->units[0]);
    }

    sim_debug(STATUS_MSG, dptr, "reset adapter.\n");

    return SCPE_OK;
}

static t_stat pmmi_svc(UNIT *uptr)
{
    int32 c,s,ireg2;
    t_stat r;
    uint32 ms;

    /* Check for new incoming connection */
    if (uptr->flags & UNIT_ATT) {
        if (tmxr_poll_conn(pmmi_ctx.tmxr) >= 0) {      /* poll connection */

            /* Clear DTR and RTS if serial port */
            if (pmmi_ctx.tmln->serport) {
                s = TMXR_MDM_DTR | ((pmmi_dev.units[0].flags & UNIT_PMMI_RTS) ? TMXR_MDM_RTS : 0);
                tmxr_set_get_modem_bits(pmmi_ctx.tmln, 0, s, NULL);
            }

            pmmi_ctx.tmln->rcve = 1;    /* Enable receiver */
            pmmi_ctx.conn = 1;          /* set connected   */

            sim_debug(STATUS_MSG, uptr->dptr, "new connection.\n");
        }
    }

    /* Update incoming modem status bits */
    if (uptr->flags & UNIT_ATT) {
        tmxr_set_get_modem_bits(pmmi_ctx.tmln, 0, 0, &s);

        ireg2 = pmmi_ctx.ireg2;
        pmmi_ctx.ireg2 &= ~PMMI_CTS;
        pmmi_ctx.ireg2 |= (s & TMXR_MDM_CTS) ? 0 : PMMI_CTS;     /* Active Low */

        /* CTS status changed */
        if ((ireg2 ^ pmmi_ctx.ireg2) & PMMI_CTS) {
            if (pmmi_ctx.ireg2 & PMMI_CTS) { /* If no CTS, set AP bit */
                pmmi_ctx.ireg2 |= PMMI_AP;   /* Answer Phone Bit (active low) */
            }
            sim_debug(STATUS_MSG, uptr->dptr, "CTS state changed to %s.\n", (pmmi_ctx.ireg2 & PMMI_CTS) ? "LOW" : "HIGH");
        }

        pmmi_ctx.ireg2 &= ~PMMI_RNG;
        pmmi_ctx.ireg2 |= (s & TMXR_MDM_RNG) ? 0 : PMMI_RNG;     /* Active Low */

        /* RNG status changed */
        if ((ireg2 ^ pmmi_ctx.ireg2) & PMMI_RNG) {
            /* Answer Phone Bit on RI */
            if (!(pmmi_ctx.ireg2 & PMMI_RNG)) {
                pmmi_ctx.ireg2 &= ~PMMI_AP;   /* Answer Phone Bit (active low) */
            }

            sim_debug(STATUS_MSG, uptr->dptr, "RNG state changed to %s.\n", (pmmi_ctx.ireg2 & PMMI_RNG) ? "LOW" : "HIGH");
        }

        /* Enable receiver if CTS is active low */
        pmmi_ctx.tmln->rcve = !(pmmi_ctx.ireg2 & PMMI_CTS);
    }

    /* TX data */
    if (pmmi_ctx.txp) {
        if (uptr->flags & UNIT_ATT) {
            if (!(pmmi_ctx.ireg2 & PMMI_CTS)) {    /* Active low */
                r = tmxr_putc_ln(pmmi_ctx.tmln, pmmi_ctx.oreg1);
                pmmi_ctx.txp = 0;               /* Reset TX Pending */
            } else {
                r = SCPE_STALL;
            }
        } else {
            r = sim_putchar(pmmi_ctx.oreg1);
            pmmi_ctx.txp = 0;               /* Reset TX Pending */
        }

        if (r == SCPE_LOST) {
            pmmi_ctx.conn = 0;          /* Connection was lost */
            sim_debug(STATUS_MSG, uptr->dptr, "lost connection.\n");
        }
    }

    /* Update TBMT if not set and no character pending */
    if (!pmmi_ctx.txp && !(pmmi_ctx.ireg0 & PMMI_TBMT)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_tx(pmmi_ctx.tmxr);
            pmmi_ctx.ireg0 |= (tmxr_txdone_ln(pmmi_ctx.tmln) && pmmi_ctx.conn) ? (PMMI_TBMT | PMMI_TEOC) : 0;
        } else {
            pmmi_ctx.ireg0 |= (PMMI_TBMT | PMMI_TEOC);
        }
    }

    /* Check for Data if RX buffer empty */
    if (!(pmmi_ctx.ireg0 & PMMI_DAV)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_rx(pmmi_ctx.tmxr);

            c = tmxr_getc_ln(pmmi_ctx.tmln);
        } else {
            c = sim_poll_kbd();
        }

        if (c & (TMXR_VALID | SCPE_KFLAG)) {
            pmmi_ctx.ireg1 = c & 0xff;
            pmmi_ctx.ireg0 |= PMMI_DAV;
            pmmi_ctx.ireg0 &= ~(PMMI_FE | PMMI_OR | PMMI_RPE);
        }
    }

    /* Timer Pulses */
    ms = sim_os_msec();

    if (ms > pmmi_ctx.ptimer) {
        if (pmmi_ctx.oreg2) {
            if (pmmi_ctx.ireg2 & PMMI_TMR) {
                pmmi_ctx.ireg2 &= ~PMMI_TMR;
                pmmi_ctx.ptimer = sim_os_msec() + 600 / (PMMI_CLOCK / pmmi_ctx.oreg2);   /* 60% off */
            } else {
                pmmi_ctx.ireg2 |= PMMI_TMR;
                pmmi_ctx.ptimer = sim_os_msec() + 400 / (PMMI_CLOCK / pmmi_ctx.oreg2);   /* 40% on */
            }
        } else {
            pmmi_ctx.ptimer = sim_os_msec() + 100;    /* default to 100ms if timer rate is 0 */
        }
    }

    /* Emulate dial tone */
    if ((ms > pmmi_ctx.dtimer) && (pmmi_ctx.oreg0 & PMMI_SH) && (pmmi_ctx.ireg2 & PMMI_DT)) {
        pmmi_ctx.ireg2 &= ~PMMI_DT;
        sim_debug(STATUS_MSG, uptr->dptr, "dial tone active.\n");
    }

    /* Don't let TMXR clobber our wait time */
    uptr->wait = PMMI_WAIT;

    sim_activate_abs(uptr, uptr->wait);

    return SCPE_OK;
}


/* Attach routine */
static t_stat pmmi_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    sim_debug(VERBOSE_MSG, uptr->dptr, "attach (%s).\n", cptr);

    if ((r = tmxr_attach(pmmi_ctx.tmxr, uptr, cptr)) == SCPE_OK) {

        pmmi_ctx.flags = uptr->flags;     /* Save Flags */

        if (!pmmi_ctx.tmln->serport) {
            uptr->flags |= UNIT_PMMI_RTS;  /* Force following DTR on sockets */
        }

        pmmi_ctx.tmln->rcve = 1;

        sim_activate(uptr, uptr->wait);

        sim_debug(VERBOSE_MSG, uptr->dptr, "activated service.\n");
    }

    return r;
}

/* Detach routine */
static t_stat pmmi_detach(UNIT *uptr)
{
    sim_debug(VERBOSE_MSG, uptr->dptr, "detach.\n");

    if (uptr->flags & UNIT_ATT) {
        uptr->flags = pmmi_ctx.flags;     /* Restore Flags */

        sim_cancel(uptr);

        return (tmxr_detach(pmmi_ctx.tmxr, uptr));
    }

    return SCPE_UNATT;
}

static t_stat pmmi_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    int32 baud;
    t_stat r = SCPE_ARG;

    if (!(uptr->flags & UNIT_ATT)) {
        return SCPE_UNATT;
    }

    if (cptr != NULL) {
        if (sscanf(cptr, "%d", &baud)) {
            if (baud >= 61 && baud <=600) {
                    pmmi_ctx.baud = baud;
                    r = pmmi_config_line(uptr);
            }
        }
    }

    return r;
}

static t_stat pmmi_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc)
{
    if (uptr->flags & UNIT_ATT) {
        fprintf(st, "Baud rate: %d", pmmi_ctx.baud);
    }

    return SCPE_OK;
}

static t_stat pmmi_config_line(UNIT *uptr)
{
    char config[20];
    char b,p,s;
    t_stat r = SCPE_IERR;

    switch (pmmi_ctx.oreg0 & PMMI_BMSK) {
        case PMMI_5BIT:
            b = '5';
            break;

        case PMMI_6BIT:
            b = '6';
            break;

        case PMMI_7BIT:
            b = '7';
            break;

        case PMMI_8BIT:
        default:
            b = '8';
            break;
    }

    switch (pmmi_ctx.oreg0 & PMMI_PMSK) {
        case PMMI_OPAR:
            p = 'O';
            break;

        case PMMI_EPAR:
            p = 'E';
            break;

        case PMMI_NPAR:
        default:
            p = 'N';
            break;
    }

    switch (pmmi_ctx.oreg0 & PMMI_SMSK) {
        case PMMI_2SB:
            s = '2';
            break;

        case PMMI_1SB:
            default:
            s = '1';
            break;
    }

    sprintf(config, "%d-%c%c%c", pmmi_ctx.baud, b,p,s);

    r = tmxr_set_config_line(pmmi_ctx.tmln, config);

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
    pmmi_ctx.tmln->txbps = 0;   /* Get TMXR's rate-limiting out of our way */
    pmmi_ctx.tmln->rxbps = 0;   /* Get TMXR's rate-limiting out of our way */

    return r;
}

static int32 pmmi_io(int32 addr, int32 io, int32 data)
{
    int32 r = 0;

    addr &= 0xff;
    data &= 0xff;

    if (io == IO_WR) {
       sim_debug(VERBOSE_MSG, &pmmi_dev, "OUT %02X,%02X\n", addr , data);
    } else {
       sim_debug(VERBOSE_MSG, &pmmi_dev, "IN %02X\n", addr);
    }

    switch (addr & 0x03) {
        case PMMI_REG0:
            r = pmmi_reg0(io, data);
            break;

        case PMMI_REG1:
            r = pmmi_reg1(io, data);
            break;

        case PMMI_REG2:
            r = pmmi_reg2(io, data);
            break;

        case PMMI_REG3:
            r = pmmi_reg3(io, data);
            break;
    }

    return(r);
}

static int32 pmmi_reg0(int32 io, int32 data)
{
    int32 r;

    if (io == IO_RD) {
        r = pmmi_ctx.ireg0;
    } else { pmmi_ctx.oreg0 = data; /* Set UART configuration */
        pmmi_config_line(&pmmi_dev.units[0]);

        if (data & PMMI_SH) {    /* If off-hook, clear dialtone bit (active low) */
            pmmi_ctx.dtimer = sim_os_msec() + 500;  /* Dialtone in 500ms */
            if (pmmi_ctx.oreg0 & PMMI_SH) {
                pmmi_ctx.ireg2 &= ~PMMI_AP;   /* Answer Phone Bit (active low) */
            }
        } else if (!(pmmi_ctx.ireg2 & PMMI_DT)) {
            pmmi_ctx.dtimer = 0;
            pmmi_ctx.ireg2 |= PMMI_DT;
            sim_debug(STATUS_MSG, &pmmi_dev, "dial tone inactive.\n");
        }

        if (data & PMMI_RI) {    /* Go off-hook in answer mode */
                pmmi_ctx.ireg2 &= ~PMMI_AP;   /* Answer Phone Bit (active low) */
        }

        r = 0x00;
    }

    return(r);
}

static int32 pmmi_reg1(int32 io, int32 data)
{
    int32 r;

    if (io == IO_RD) {
        r = pmmi_ctx.ireg1;
        pmmi_ctx.ireg0 &= ~(PMMI_DAV | PMMI_FE | PMMI_OR | PMMI_RPE);
    } else {
        pmmi_ctx.oreg1 = data;
        pmmi_ctx.ireg0 &= ~(PMMI_TBMT | PMMI_TEOC);
        pmmi_ctx.txp = 1;

        r = 0x00;
    }

    return(r);
}

static int32 pmmi_reg2(int32 io, int32 data)
{
    int32 r;

    if (io == IO_RD) {
        r = pmmi_ctx.ireg2;
    } else {
        pmmi_ctx.oreg2 = data;

        /*
        ** The actual baud rate is determined by the following:
        ** Rate = 250,000/(Reg X 16) where Reg = the binary
        ** value loaded into the rate generator.
        */
        if (data) {
            pmmi_ctx.baud = 250000/(data * 16);

            pmmi_config_line(&pmmi_dev.units[0]);
        }

        r = 0x00;
    }

    return(r);
}

static int32 pmmi_reg3(int32 io, int32 data)
{
    int32 s;

    if (io == IO_RD) {
        pmmi_ctx.intmsk = pmmi_ctx.oreg2;  /* Load int mask from rate generator */
    } else {
        pmmi_ctx.oreg3 = data;
        /* Set/Clear DTR */
        s = TMXR_MDM_DTR | ((pmmi_dev.units[0].flags & UNIT_PMMI_RTS) ? TMXR_MDM_RTS : 0);
        if (data & PMMI_DTR) {
            tmxr_set_get_modem_bits(pmmi_ctx.tmln, s, 0, NULL);
            if (pmmi_ctx.oreg0 & PMMI_SH) {
                pmmi_ctx.ireg2 &= ~PMMI_AP;   /* Answer Phone Bit (active low) */
            }
            sim_debug(STATUS_MSG, &pmmi_dev, "set DTR HIGH.\n");
        } else {
            tmxr_set_get_modem_bits(pmmi_ctx.tmln, 0, s, NULL);
            pmmi_ctx.ireg2 |= PMMI_AP;
            sim_debug(STATUS_MSG, &pmmi_dev, "set DTR LOW.\n");
        }
    }
    return 0x00;
}

