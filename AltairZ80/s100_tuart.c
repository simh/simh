/* s100_tuart.c: Cromemco TU-ART

   Copyright (c) 2024, Patrick Linstruth

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

*/

#include "altairz80_defs.h"
#include "sim_tmxr.h"

#define TUART_NAME  "CROMEMCO TU-ART"
#define TUART0_SNAME "TUART0"
#define TUART1_SNAME "TUART1"
#define TUART2_SNAME "TUART2"

#define TUART_WAIT        1000           /* Service Wait Interval */

#define TUART0_IOBASE     0x00
#define TUART0_IOSIZE     4
#define TUART1_IOBASE     0x20
#define TUART1_IOSIZE     4
#define TUART2_IOBASE     0x50
#define TUART2_IOSIZE     4

/* Status Register */
#define TUART_FME         0x01           /* Framing Error                */
#define TUART_ORE         0x02           /* Overrun                      */
#define TUART_IPG         0x20           /* Interrupt Pending            */
#define TUART_RDA         0x40           /* Receive Data Available       */
#define TUART_TBE         0x80           /* Transmit Buffer Empty        */

/* Command Register */
#define TUART_RESET       0x01           /* Reset                        */
#define TUART_INTA        0x08           /* Interrupt Enable             */
#define TUART_HBD         0x10           /* High baud rate               */

#define TUART_110         0x01
#define TUART_150         0x02
#define TUART_300         0x04
#define TUART_1200        0x08
#define TUART_2400        0x10
#define TUART_4800        0x20
#define TUART_9600        0x40
#define TUART_1STOP       0x80

#define TUART_BAUD(xptr)  ((xptr->baud * xptr->hbd > 76800) ? 76800 : xptr->baud * xptr->hbd)

#define TUART_RDAIE       0x10
#define TUART_TBEIE       0x20

#define TUART_RDAIA       0xe7
#define TUART_TBSIA       0xef

/* Debug flags */
#define STATUS_MSG        (1 << 0)
#define IRQ_MSG           (1 << 1)
#define ERROR_MSG         (1 << 2)
#define VERBOSE_MSG       (1 << 3)

/* IO Read/Write */
#define IO_RD            0x00     /* IO Read  */
#define IO_WR            0x01     /* IO Write */

typedef struct {
    PNP_INFO pnp;        /* Must be first     */
    t_bool conn;         /* Connected Status  */
    TMLN *tmln;          /* TMLN pointer      */
    TMXR *tmxr;          /* TMXR pointer      */
    int32 baud;          /* Baud rate         */
    uint8 hbd;           /* High baud mult    */
    uint8 sbits;         /* Stop bits         */
    uint8 rxb;           /* Receive Buffer    */
    uint8 txb;           /* Transmit Buffer   */
    t_bool txp;          /* Transmit Pending  */
    uint8 stb;           /* Status Buffer     */
    t_bool inta;         /* Interrupt Ack Ena */
    uint8 intmask;       /* Int Enable Mask   */
    uint8 intadr;        /* Interrupt Address */
    uint8 intvector;     /* Interrupt Vector  */
} TUART_CTX;

extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);


static const char* tuart_description(DEVICE *dptr);
static t_stat tuart_svc(UNIT *uptr);
static t_stat tuart_reset(DEVICE *dptr, int32 (*routine)(const int32, const int32, const int32));
static t_stat tuart0_reset(DEVICE *dptr);
static t_stat tuart1_reset(DEVICE *dptr);
static t_stat tuart2_reset(DEVICE *dptr);
static t_stat tuart_attach(UNIT *uptr, CONST char *cptr);
static t_stat tuart_detach(UNIT *uptr);
static t_stat tuart_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat tuart_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc);
static t_stat tuart_config_line(UNIT *uptr);
static int32 tuart0_io(int32 addr, int32 io, int32 data);
static int32 tuart1_io(int32 addr, int32 io, int32 data);
static int32 tuart2_io(int32 addr, int32 io, int32 data);
static int32 tuart_io(DEVICE *dptr, int32 addr, int32 io, int32 data);
static int32 tuart_stat(DEVICE *dptr, int32 io, int32 data);
static int32 tuart_data(DEVICE *dptr, int32 io, int32 data);
static int32 tuart_command(DEVICE *dptr, int32 io, int32 data);
static int32 tuart_intadrmsk(DEVICE *dptr, int32 io, int32 data);
static void tuart_int(UNIT *uptr);

extern uint32 vectorInterrupt;          /* Vector Interrupt bits */
extern uint8 dataBus[MAX_INT_VECTORS];  /* Data bus value        */

/* Debug Flags */
static DEBTAB tuart_dt[] = {
    { "STATUS",         STATUS_MSG,         "Status messages"  },
    { "IRQ",            IRQ_MSG,            "Interrupt messages"  },
    { "ERROR",          ERROR_MSG,          "Error messages"  },
    { "VERBOSE",        VERBOSE_MSG,        "Verbose messages"  },
    { NULL,             0                   }
};

/* Terminal multiplexer library descriptors */

static TMLN tuart0_tmln[] = {         /* line descriptors */
    { 0 }
};

static TMLN tuart1_tmln[] = {         /* line descriptors */
    { 0 }
};

static TMLN tuart2_tmln[] = {         /* line descriptors */
    { 0 }
};

static TMXR tuart0_tmxr = {           /* multiplexer descriptor */
    1,                                /* number of terminal lines */
    0,                                /* listening port (reserved) */
    0,                                /* master socket  (reserved) */
    tuart0_tmln,                      /* line descriptor array */
    NULL,                             /* line connection order */
    NULL                              /* multiplexer device (derived internally) */
};

static TMXR tuart1_tmxr = {           /* multiplexer descriptor */
    1,                                /* number of terminal lines */
    0,                                /* listening port (reserved) */
    0,                                /* master socket  (reserved) */
    tuart1_tmln,                      /* line descriptor array */
    NULL,                             /* line connection order */
    NULL                              /* multiplexer device (derived internally) */
};

static TMXR tuart2_tmxr = {           /* multiplexer descriptor */
    1,                                /* number of terminal lines */
    0,                                /* listening port (reserved) */
    0,                                /* master socket  (reserved) */
    tuart2_tmln,                      /* line descriptor array */
    NULL,                             /* line connection order */
    NULL                              /* multiplexer device (derived internally) */
};

#define UNIT_V_TUART_CONSOLE  (UNIT_V_UF + 0)     /* Port checks console for input */
#define UNIT_TUART_CONSOLE    (1 << UNIT_V_TUART_CONSOLE)
#define UNIT_V_TUART_EVEN     (UNIT_V_UF + 1)     /* Mode 2 interrupts even mode */
#define UNIT_TUART_EVEN       (1 << UNIT_V_TUART_EVEN)

static MTAB tuart_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,    0,                      "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets TU-ART base I/O address"   },
    { UNIT_TUART_CONSOLE,   UNIT_TUART_CONSOLE, "CONSOLE",   "CONSOLE",   NULL, NULL, NULL,
        "Port checks for console input" },
    { UNIT_TUART_CONSOLE,   0,                  "NOCONSOLE", "NOCONSOLE", NULL, NULL, NULL,
        "Port does not check for console input" },
    { UNIT_TUART_EVEN,   UNIT_TUART_EVEN, "EVEN",   "EVEN",   NULL, NULL, NULL,
        "Mode 2 interrupt even mode" },
    { UNIT_TUART_EVEN,   0,               "ODD",    "ODD",    NULL, NULL, NULL,
        "Mode 2 interrupt odd mode" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,  0,    "BAUD",  "BAUD",  &tuart_set_baud, &tuart_show_baud,
        NULL, "Set baud rate (default=9600)" },
    { 0 }
};

static TUART_CTX tuart0_ctx = {{0, 0, TUART0_IOBASE, TUART0_IOSIZE}, 0, tuart0_tmln, &tuart0_tmxr};
static TUART_CTX tuart1_ctx = {{0, 0, TUART1_IOBASE, TUART1_IOSIZE}, 0, tuart1_tmln, &tuart1_tmxr};
static TUART_CTX tuart2_ctx = {{0, 0, TUART2_IOBASE, TUART2_IOSIZE}, 0, tuart2_tmln, &tuart2_tmxr};

static UNIT tuart0_unit[] = {
        { UDATA (&tuart_svc, UNIT_ATTABLE | UNIT_DISABLE | UNIT_TUART_CONSOLE, 0), TUART_WAIT },
};
static UNIT tuart1_unit[] = {
        { UDATA (&tuart_svc, UNIT_ATTABLE | UNIT_DISABLE, 0), TUART_WAIT },
};
static UNIT tuart2_unit[] = {
        { UDATA (&tuart_svc, UNIT_ATTABLE | UNIT_DISABLE, 0), TUART_WAIT },
};

static REG tuart0_reg[] = {
    { HRDATAD (INTMASK0, tuart0_ctx.intmask, 8, "TU-ART port 0 interrupt mask"), },
    { DRDATAD (INTVEC0, tuart0_ctx.intvector, 8, "TU-ART port 0 interrupt vector"), },
    { HRDATAD (INTADR0, tuart0_ctx.intadr, 8, "TU-ART port 0 interrupt address"), },
    { NULL }
};
static REG tuart1_reg[] = {
    { HRDATAD (INTMASK1, tuart1_ctx.intmask, 8, "TU-ART port 1/A interrupt mask"), },
    { DRDATAD (INTVEC1, tuart1_ctx.intvector, 8, "TU-ART port 1/A interrupt vector"), },
    { HRDATAD (INTADR1, tuart1_ctx.intadr, 8, "TU-ART port 1/A interrupt address"), },
    { NULL }
};
static REG tuart2_reg[] = {
    { HRDATAD (INTMASK2, tuart2_ctx.intmask, 8, "TU-ART port 2/B interrupt mask"), },
    { DRDATAD (INTVEC2, tuart2_ctx.intvector, 8, "TU-ART port 2/B interrupt vector"), },
    { HRDATAD (INTADR2, tuart2_ctx.intadr, 8, "TU-ART port 2/B interrupt address"), },
    { NULL }
};

DEVICE tuart0_dev = {
    TUART0_SNAME,       /* name */
    tuart0_unit,        /* unit */
    tuart0_reg,         /* registers */
    tuart_mod,          /* modifiers */
    1,                  /* # units */
    10,                 /* address radix */
    31,                 /* address width */
    1,                  /* address increment */
    8,                  /* data radix */
    8,                  /* data width */
    NULL,               /* examine routine */
    NULL,               /* deposit routine */
    &tuart0_reset,      /* reset routine */
    NULL,               /* boot routine */
    &tuart_attach,      /* attach routine */
    &tuart_detach,      /* detach routine */
    &tuart0_ctx,        /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX),  /* flags */
    0,                  /* debug control */
    tuart_dt,           /* debug flags */
    NULL,               /* mem size routine */
    NULL,               /* logical name */
    NULL,               /* help */
    NULL,               /* attach help */
    NULL,               /* context for help */
    &tuart_description  /* description */
};

DEVICE tuart1_dev = {
    TUART1_SNAME,       /* name */
    tuart1_unit,        /* unit */
    tuart1_reg,         /* registers */
    tuart_mod,          /* modifiers */
    1,                  /* # units */
    10,                 /* address radix */
    31,                 /* address width */
    1,                  /* address increment */
    8,                  /* data radix */
    8,                  /* data width */
    NULL,               /* examine routine */
    NULL,               /* deposit routine */
    &tuart1_reset,      /* reset routine */
    NULL,               /* boot routine */
    &tuart_attach,      /* attach routine */
    &tuart_detach,      /* detach routine */
    &tuart1_ctx,        /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX),  /* flags */
    0,                  /* debug control */
    tuart_dt,           /* debug flags */
    NULL,               /* mem size routine */
    NULL,               /* logical name */
    NULL,               /* help */
    NULL,               /* attach help */
    NULL,               /* context for help */
    &tuart_description  /* description */
};

DEVICE tuart2_dev = {
    TUART2_SNAME,       /* name */
    tuart2_unit,        /* unit */
    tuart2_reg,         /* registers */
    tuart_mod,          /* modifiers */
    1,                  /* # units */
    10,                 /* address radix */
    31,                 /* address width */
    1,                  /* address increment */
    8,                  /* data radix */
    8,                  /* data width */
    NULL,               /* examine routine */
    NULL,               /* deposit routine */
    &tuart2_reset,      /* reset routine */
    NULL,               /* boot routine */
    &tuart_attach,      /* attach routine */
    &tuart_detach,      /* detach routine */
    &tuart2_ctx,        /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX),  /* flags */
    0,                  /* debug control */
    tuart_dt,           /* debug flags */
    NULL,               /* mem size routine */
    NULL,               /* logical name */
    NULL,               /* help */
    NULL,               /* attach help */
    NULL,               /* context for help */
    &tuart_description  /* description */
};

static const char* tuart_description(DEVICE *dptr)
{
    return TUART_NAME;
}

static t_stat tuart0_reset(DEVICE *dptr)
{
    return(tuart_reset(dptr, &tuart0_io));
}

static t_stat tuart1_reset(DEVICE *dptr)
{
    return(tuart_reset(dptr, &tuart1_io));
}

static t_stat tuart2_reset(DEVICE *dptr)
{
    return(tuart_reset(dptr, &tuart2_io));
}

static t_stat tuart_reset(DEVICE *dptr, int32 (*routine)(const int32, const int32, const int32))
{
    TUART_CTX *xptr;

    xptr = (TUART_CTX *) dptr->ctxt;

    /* Connect/Disconnect I/O Ports at base address */
    if (sim_map_resource(xptr->pnp.io_base, xptr->pnp.io_size, RESOURCE_TYPE_IO, routine, dptr->name, dptr->flags & DEV_DIS) != 0) {
        sim_debug(ERROR_MSG, dptr, "error mapping I/O resource at 0x%02x.\n", xptr->pnp.io_base);
        return SCPE_ARG;
    }

    /* Set DEVICE for this UNIT */
    dptr->units[0].dptr = dptr;

    /* Reset registers */
    xptr->stb = 0x00;
    xptr->txp = FALSE;
    xptr->hbd = 1;
    xptr->baud = 9600;
    xptr->sbits = 1;

    tuart_config_line(&dptr->units[0]);

    if (!(dptr->flags & DEV_DIS) && dptr->units[0].flags & UNIT_TUART_CONSOLE) {
        sim_activate_after_abs(&dptr->units[0], dptr->units[0].wait);
    } else {
        sim_cancel(&dptr->units[0]);
    }

    sim_debug(STATUS_MSG, dptr, "reset adapter.\n");

    return SCPE_OK;
}


static t_stat tuart_svc(UNIT *uptr)
{
    TUART_CTX *xptr;
    int32 c;
    t_stat r;

    xptr = (TUART_CTX *) uptr->dptr->ctxt;

    /* Check for new incoming connection */
    if (uptr->flags & UNIT_ATT) {
        if (tmxr_poll_conn(xptr->tmxr) >= 0) {      /* poll connection */

            xptr->conn = TRUE;          /* set connected   */

            sim_debug(STATUS_MSG, uptr->dptr, "new connection.\n");
        }
    }

    /* Update incoming modem status bits */
    if (uptr->flags & UNIT_ATT) {
        xptr->stb = 0x00;

        /* Enable receiver if DCD is active low */
        xptr->tmln->rcve = 1;
    }

    /* TX data */
    if (xptr->txp) {
        if (uptr->flags & UNIT_ATT) {
            r = tmxr_putc_ln(xptr->tmln, xptr->txb);
            xptr->txp = FALSE;             /* Reset TX Pending */
        } else {
            r = sim_putchar(xptr->txb);
            xptr->txp = FALSE;                 /* Reset TX Pending */
        }

        if (r == SCPE_LOST) {
            xptr->conn = FALSE;          /* Connection was lost */
            sim_debug(STATUS_MSG, uptr->dptr, "lost connection.\n");
        }

        /* If TX buffer now empty, send interrupt */
        if ((!xptr->txp) && (xptr->intmask & TUART_TBEIE)) {
            xptr->intadr = TUART_TBSIA;
            tuart_int(uptr);
        }

    }

    /* Update TBE if not set and no character pending */
    if (!xptr->txp && !(xptr->stb & TUART_TBE)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_tx(xptr->tmxr);
            xptr->stb |= (tmxr_txdone_ln(xptr->tmln) && xptr->conn) ? TUART_TBE : 0;
        } else {
            xptr->stb |= TUART_TBE;
        }
    }

    /* Check for Data if RX buffer empty */
    if (!(xptr->stb & TUART_RDA)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_rx(xptr->tmxr);

            c = tmxr_getc_ln(xptr->tmln);
        } else if (uptr->flags & UNIT_TUART_CONSOLE) {
            c = sim_poll_kbd();
        } else {
            c = 0;
        }

        if (c & (TMXR_VALID | SCPE_KFLAG)) {
            xptr->rxb = c & 0xff;
            xptr->stb |= TUART_RDA;
            xptr->stb &= ~(TUART_FME | TUART_ORE);
            if (xptr->intmask & TUART_RDAIE) {
                xptr->intadr = TUART_RDAIA;
                tuart_int(uptr);
            }
        }
    }

    sim_activate_after_abs(uptr, uptr->wait);

    return SCPE_OK;
}


/* Attach routine */
static t_stat tuart_attach(UNIT *uptr, CONST char *cptr)
{
    TUART_CTX *xptr;
    t_stat r = SCPE_OK;

    xptr = (TUART_CTX *) uptr->dptr->ctxt;

    sim_debug(VERBOSE_MSG, uptr->dptr, "attach (%s).\n", cptr);

    if ((r = tmxr_attach(xptr->tmxr, uptr, cptr)) == SCPE_OK) {
        xptr->tmln->rcve = 1;

        r = tuart_config_line(uptr);

        sim_activate_after_abs(uptr, uptr->wait);
    }

    return r;
}


/* Detach routine */
static t_stat tuart_detach(UNIT *uptr)
{
    TUART_CTX *xptr;

    if (uptr->dptr == NULL) {
        return SCPE_IERR;
    }

    sim_debug(VERBOSE_MSG, uptr->dptr, "detach.\n");

    if (uptr->flags & UNIT_ATT) {
        xptr = (TUART_CTX *) uptr->dptr->ctxt;

        if (uptr->flags & UNIT_TUART_CONSOLE) {
            uptr->wait = TUART_WAIT;
        } else {
            sim_cancel(uptr);
        }

        return (tmxr_detach(xptr->tmxr, uptr));
    }

    return SCPE_UNATT;
}

static t_stat tuart_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    TUART_CTX *xptr;
    int32 baud;
    t_stat r = SCPE_ARG;

    xptr = (TUART_CTX *) uptr->dptr->ctxt;

    if (cptr != NULL) {
        if (sscanf(cptr, "%d", &baud)) {
            switch (baud) {
                case 110:
                case 150:
                case 300:
                case 1200:
                case 2400:
                case 4800:
                case 9600:
                    xptr->baud = baud;
                    xptr->hbd = 1;
                    r = tuart_config_line(uptr);
                    return r;

                case 19200:
                case 38400:
                case 76800:
                    xptr->baud = baud / 8;
                    xptr->hbd = 8;
                    r = tuart_config_line(uptr);
                    return r;

                default:
                    sim_printf("invalid baud rate\n");
                    break;
            }
        }
    }

    return r;
}

static t_stat tuart_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc)
{
    TUART_CTX *xptr;

    xptr = (TUART_CTX *) uptr->dptr->ctxt;

    fprintf(st, "%d (wait=%d)", TUART_BAUD(xptr), uptr->wait);

    return SCPE_OK;
}

static t_stat tuart_config_line(UNIT *uptr)
{
    TUART_CTX *xptr;
    char config[20];
    t_stat r = SCPE_OK;

    xptr = (TUART_CTX *) uptr->dptr->ctxt;

    if (xptr != NULL) {
        sprintf(config, "%d-8N%d", TUART_BAUD(xptr), xptr->sbits);

        if (uptr->flags & UNIT_ATT) {
            r = tmxr_set_config_line(xptr->tmln, config);

            if (xptr->tmln->serport) {
                uptr->wait = 9600000 / TUART_BAUD(xptr);
            } else {
                uptr->wait = TUART_WAIT;
            }

            xptr->tmln->txbps = 0;   /* Get TMXR's timing out of our way */
            xptr->tmln->rxbps = 0;   /* Get TMXR's timing out of our way */
        }

        sim_debug(STATUS_MSG, uptr->dptr, "Port configuration set to '%s'.\n", config);
    }

    return r;
}

static int32 tuart0_io(int32 addr, int32 io, int32 data)
{
    return(tuart_io(&tuart0_dev, addr, io, data));
}

static int32 tuart1_io(int32 addr, int32 io, int32 data)
{
    return(tuart_io(&tuart1_dev, addr, io, data));
}

static int32 tuart2_io(int32 addr, int32 io, int32 data)
{
    return(tuart_io(&tuart2_dev, addr, io, data));
}

static int32 tuart_io(DEVICE *dptr, int32 addr, int32 io, int32 data)
{
    int32 r;

    if ((addr & 0x03) == 0x03) {
        r = tuart_intadrmsk(dptr, io, data);
    }
    else if (addr & 0x02) {
        r = tuart_command(dptr, io, data);
    }
    else if (addr & 0x01) {
        r = tuart_data(dptr, io, data);
    } else {
        r = tuart_stat(dptr, io, data);
    }

    return(r);
}

static int32 tuart_stat(DEVICE *dptr, int32 io, int32 data)
{
    TUART_CTX *xptr;
    int32 r = 0xff;

    xptr = (TUART_CTX *) dptr->ctxt;

    if (io == IO_RD) {
        r = xptr->stb;
    } else {
        xptr->sbits = (data & TUART_1STOP) ? 1 : 2;

        switch (data & ~TUART_1STOP) {
            case TUART_110:
                xptr->baud = 110;
                break;

            case TUART_150:
                xptr->baud = 150;
                break;

            case TUART_300:
                xptr->baud = 300;
                break;

            case TUART_1200:
                xptr->baud = 1200;
                break;

            case TUART_2400:
                xptr->baud = 2400;
                break;

            case TUART_4800:
                xptr->baud = 4800;
                break;

            case TUART_9600:
            default:
                xptr->baud = 9600;
                break;
        }
        sim_debug(STATUS_MSG, dptr, "Status Port Write %02X (sbits=%d baud=%d)\n", data, xptr->sbits, xptr->baud);
        tuart_config_line(&dptr->units[0]);
    }

    return r;
}

static int32 tuart_data(DEVICE *dptr, int32 io, int32 data)
{
    TUART_CTX *xptr;
    int32 r = 0xff;

    xptr = (TUART_CTX *) dptr->ctxt;

    if (io == IO_RD) {
        r = xptr->rxb;
        xptr->stb &= ~(TUART_RDA | TUART_FME | TUART_ORE | TUART_IPG);
    } else {
        xptr->txb = data;
        xptr->stb &= ~(TUART_TBE | TUART_IPG);
        xptr->txp = TRUE;
    }

    return r;
}

static int32 tuart_command(DEVICE *dptr, int32 io, int32 data)
{
    TUART_CTX *xptr;
    int32 r = 0xff;

    xptr = (TUART_CTX *) dptr->ctxt;

    if (io == IO_RD) {
    } else {
        if (data & TUART_RESET) {
            xptr->stb &= ~(TUART_ORE);
            sim_debug(STATUS_MSG, dptr, "Reset port\n");
        }
        xptr->inta = (data & TUART_INTA) ? TRUE : FALSE;
        xptr->hbd = (data & TUART_HBD) ? 8 : 1;
        sim_debug(STATUS_MSG, dptr, "Command Port Write %02X (inta=%d hbd=%d)\n", data, xptr->inta, xptr->hbd);
        tuart_config_line(&dptr->units[0]);
    }

    return r;
}

static int32 tuart_intadrmsk(DEVICE *dptr, int32 io, int32 data)
{
    TUART_CTX *xptr;

    xptr = (TUART_CTX *) dptr->ctxt;

    if (io == IO_RD) {
        return xptr->intadr;
    } else {
        xptr->intmask = data;
    }

    return 0xff;
}

static void tuart_int(UNIT *uptr)
{
    TUART_CTX *xptr;

    xptr = (TUART_CTX *) uptr->dptr->ctxt;

    if (!xptr->inta) {
        return;
    }

    vectorInterrupt |= (1 << xptr->intvector);
    dataBus[xptr->intvector] = xptr->intadr;
    if (uptr->flags & UNIT_TUART_EVEN) {
        dataBus[xptr->intvector] &= 0xfe;
    }
    xptr->stb |= TUART_IPG;

    sim_debug(IRQ_MSG, uptr->dptr, "Vector=%d Data bus=%02X\n", xptr->intvector, dataBus[xptr->intvector]);
}

