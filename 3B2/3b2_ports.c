/* 3b2_ports.c: AT&T 3B2 Model 400 "PORTS" feature card

   Copyright (c) 2018, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

/*
 * PORTS is an intelligent feature card for the 3B2 that supports four
 * serial lines and one Centronics parallel port.
 *
 * The PORTS card is based on the Common I/O (CIO) platform. It uses
 * two SCN2681A DUARTs to supply the four serial lines, and uses the
 * SCN2681A parallel I/O pins for the Centronics parallel port.
 *
 * We make no attempt to emulate a PORTS card's internal workings
 * precisely. Instead, we treat it as a black box as seen from the 3B2
 * system board's point of view.
 *
 */

#include "3b2_defs.h"
#include "3b2_ports.h"

/* Static function declarations */
static t_stat ports_show_queue_common(FILE *st, UNIT *uptr, int32 val,
                                      CONST void *desc, t_bool rq);

/* Device and units for PORTS cards
 * --------------------------------
 *
 * A 3B2/400 system can have up to 12 PORTS cards installed.  Each
 * card, in turn, has 5 TTY devices - four serial TTYs and one
 * parallel printer port (the printer port is not supported at this
 * time, and is a no-op).
 *
 * The PORTS emulator is backed by a terminal multiplexer with up to
 * 48 (12 * 4) serial lines. Lines can be specified with:
 *
 *   SET PORTS LINES=n
 *
 * Lines must be specified in multiples of 4.
 *
 * Implementation8
 * --------------
 *
 * Each set of 4 lines is mapped to a CIO_STATE struct in the "cio"
 * CIO_STATE structure.
 *
 */

#define PPQESIZE      12
#define DELAY_ASYNC   25
#define DELAY_DLM     100
#define DELAY_ULM     100
#define DELAY_FCF     100
#define DELAY_DOS     100
#define DELAY_DSD     100
#define DELAY_OPTIONS 100
#define DELAY_VERS    100
#define DELAY_CONN    100
#define DELAY_XMIT    50
#define DELAY_RECV    25
#define DELAY_DEVICE  25
#define DELAY_STD     100

#define PORTS_DIAG_CRC1 0x7ceec900
#define PORTS_DIAG_CRC2 0x77a1ea56
#define PORTS_DIAG_CRC3 0x84cf938b

#define LN(cid,port)   ((PORTS_LINES * ((cid) - ports_base_cid)) + (port))
#define LCID(ln)       (((ln) / PORTS_LINES) + ports_base_cid)
#define LPORT(ln)      ((ln) % PORTS_LINES)

int8    ports_base_cid;            /* First cid in our contiguous block */
uint8   ports_int_cid;             /* Interrupting card ID   */
uint8   ports_int_subdev;          /* Interrupting subdevice */
t_bool  ports_conf = FALSE;  /* Have PORTS cards been configured? */
uint32  ports_crc;           /* CRC32 of downloaded memory */

/* PORTS-specific state for each slot */
PORTS_LINE_STATE *ports_state = NULL;

/* Baud rates determined by the low nybble
 * of the PORT_OPTIONS cflag */
CONST char *ports_baud[16] = {
    "75",    "110",  "134",  "150",
    "300",   "600",  "1200", "2000",
    "2400",  "4800", "1800", "9600",
    "19200", "9600", "9600", "9600"
};

TMLN *ports_ldsc = NULL;
TMXR ports_desc = { 0, 0, 0, NULL };

/* Three units service the Receive, Transmit, and CIO */
UNIT ports_unit[3] = {
    { UDATA(&ports_rcv_svc, UNIT_IDLE|UNIT_ATTABLE|TT_MODE_8B, 0) },
    { UDATA(&ports_xmt_svc, UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA(&ports_cio_svc, UNIT_DIS, 0) }
};

MTAB ports_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL, "7 bit mode" },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL, NULL, NULL, "8 bit mode" },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL, NULL, NULL, "7 bit mode - non printing suppressed" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "RQUEUE=n", NULL,
      NULL, &ports_show_rqueue, NULL, "Display Request Queue for card n" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "CQUEUE=n", NULL,
      NULL, &ports_show_cqueue, NULL, "Display Completion Queue for card n" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
      &ports_setnl, &tmxr_show_lines, (void *) &ports_desc, "Display number of lines" },
    { 0 }
};

static DEBTAB ports_debug[] = {
    { "IO",    IO_DBG,        "I/O Character Trace"          },
    { "TRACE", TRACE_DBG,     "Call Trace"                   },
    { "XMT",   TMXR_DBG_XMT,  "TMXR Transmit Data"           },
    { "RCV",   TMXR_DBG_RCV,  "TMXR Received Data"           },
    { "RET",   TMXR_DBG_RET,  "TMXR Returned Received Data"  },
    { "MDM",   TMXR_DBG_XMT,  "TMXR Modem Signals"           },
    { "CON",   TMXR_DBG_XMT,  "TMXR Connection Activity"     },
    { "ASY",   TMXR_DBG_ASY,  "TMXR Async Activity"          },
    { "PXMT",  TMXR_DBG_PXMT, "TMXR Transmit Packets"        },
    { "PRCV",  TMXR_DBG_PRCV, "TMXR Received Packets"        },
    { NULL }
};

DEVICE ports_dev = {
    "PORTS",                       /* name */
    ports_unit,                    /* units */
    NULL,                          /* registers */
    ports_mod,                     /* modifiers */
    3,                             /* #units */
    16,                            /* address radix */
    32,                            /* address width */
    1,                             /* address incr. */
    16,                            /* data radix */
    8,                             /* data width */
    NULL,                          /* examine routine */
    NULL,                          /* deposit routine */
    &ports_reset,                  /* reset routine */
    NULL,                          /* boot routine */
    &ports_attach,                 /* attach routine */
    &ports_detach,                 /* detach routine */
    NULL,                          /* context */
    DEV_DISABLE|DEV_DIS|DEV_DEBUG|DEV_MUX, /* flags */
    0,                             /* debug control flags */
    ports_debug,                   /* debug flag names */
    NULL,                          /* memory size change */
    NULL,                          /* logical name */
    NULL,                          /* help routine */
    NULL,                          /* attach help routine */
    (void *)&ports_desc,           /* help context */
    NULL,                          /* device description */
};


static void cio_irq(uint8 cid, uint8 dev, int32 delay)
{
    ports_int_cid = cid;
    ports_int_subdev = dev & 0xf;
    sim_activate(&ports_unit[2], delay);
}

/*
 * Set the number of lines for the PORTS mux. This will add or remove
 * cards as necessary. The number of lines must be a multiple of 4.
 */
t_stat ports_setnl(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 newln, i, t;
    t_stat r = SCPE_OK;

    if (cptr == NULL) {
        return SCPE_ARG;
    }

    newln = (int32) get_uint(cptr, 10, (MAX_PORTS_CARDS * PORTS_LINES), &r);

    if ((r != SCPE_OK) || (newln == ports_desc.lines)) {
        return r;
    }

    if ((newln == 0) || LPORT(newln) != 0) {
        return SCPE_ARG;
    }

    if (newln < ports_desc.lines) {
        for (i = newln, t = 0; i < ports_desc.lines; i++) {
            t = t | ports_ldsc[i].conn;
        }

        if (t && !get_yn("This will disconnect users; proceed [N]?", FALSE)) {
            return SCPE_OK;
        }

        for (i = newln; i < ports_desc.lines; i++) {
            if (ports_ldsc[i].conn) {
                tmxr_linemsg(&ports_ldsc[i], "\r\nOperator disconnected line\r\n");
                tmxr_send_buffered_data(&ports_ldsc[i]);
            }
            /* completely reset line */
            tmxr_detach_ln(&ports_ldsc[i]);
            if (LPORT(i) == (PORTS_LINES - 1)) {
                /* Also drop the corresponding card from the CIO array */
                cio_clear(LCID(i));
            }
        }
    }

    ports_desc.ldsc = ports_ldsc = (TMLN *)realloc(ports_ldsc, newln*sizeof(*ports_ldsc));
    ports_state = (PORTS_LINE_STATE *)realloc(ports_state, newln*sizeof(*ports_state));

    if (ports_desc.lines < newln) {
        memset(ports_ldsc + ports_desc.lines, 0, sizeof(*ports_ldsc)*(newln-ports_desc.lines));
        memset(ports_state + ports_desc.lines, 0, sizeof(*ports_state)*(newln-ports_desc.lines));
    }

    ports_desc.lines = newln;

    /* setup lines and auto config */
    ports_conf = FALSE;
    return ports_reset(&ports_dev);
}


static void ports_cmd(uint8 cid, cio_entry *rentry, uint8 *rapp_data)
{
    cio_entry centry = {0};
    uint32 ln, i;
    PORTS_OPTIONS opts;
    char line_config[16];
    uint8 app_data[4] = {0};

    centry.address = rentry->address;
    cio[cid].op = rentry->opcode;
    ln = LN(cid, rentry->subdevice & 0xf);

    switch(rentry->opcode) {
    case CIO_DLM:
        for (i = 0; i < rentry->byte_count; i++) {
            ports_crc = cio_crc32_shift(ports_crc, pread_b(rentry->address + i));
        }
        centry.address = rentry->address + rentry->byte_count;
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] CIO Download Memory: bytecnt=%04x "
                  "addr=%08x return_addr=%08x subdev=%02x (CRC=%08x)\n",
                  R[NUM_PC],
                  rentry->byte_count, rentry->address,
                  centry.address, centry.subdevice, ports_crc);
        /* We intentionally do not set the subdevice in
         * the completion entry */
        cio_cexpress(cid, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_DLM);
        break;
    case CIO_ULM:
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] CIO Upload Memory\n",
                  R[NUM_PC]);
        cio_cexpress(cid, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_ULM);
        break;
    case CIO_FCF:
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] CIO Force Function Call (CRC=%08x)\n",
                  R[NUM_PC], ports_crc);

        /* If the currently running program is a diagnostics program,
         * we are expected to write results into memory at address
         * 0x200f000 */
        if (ports_crc == PORTS_DIAG_CRC1 ||
            ports_crc == PORTS_DIAG_CRC2 ||
            ports_crc == PORTS_DIAG_CRC3) {
            pwrite_h(0x200f000, 0x1);   /* Test success */
            pwrite_h(0x200f002, 0x0);   /* Test Number */
            pwrite_h(0x200f004, 0x0);   /* Actual */
            pwrite_h(0x200f006, 0x0);   /* Expected */
            pwrite_b(0x200f008, 0x1);   /* Success flag again */
        }

        /* An interesting (?) side-effect of FORCE FUNCTION CALL is
         * that it resets the card state such that a new SYSGEN is
         * required in order for new commands to work. In fact, an
         * INT0/INT1 combo _without_ a RESET can sysgen the board. So,
         * we reset the command bits here. */
        cio[cid].sysgen_s = 0;
        cio_cexpress(cid, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_FCF);
        break;
    case CIO_DOS:
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] CIO Determine Op Status\n",
                  R[NUM_PC]);
        cio_cexpress(cid, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_DOS);
        break;
    case CIO_DSD:
        /* Determine Sub-Devices. We have none. */
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] Determine Sub-Devices.\n",
                  R[NUM_PC]);

        /* The system wants us to write sub-device structures
         * at the supplied address */

        pwrite_h(rentry->address, 0x0);
        cio_cexpress(cid, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_DSD);
        break;
    case PPC_OPTIONS:
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] PPC Options Operation\n",
                  R[NUM_PC]);

        opts.line   = pread_h(rentry->address);
        opts.iflag  = pread_h(rentry->address + 4);
        opts.oflag  = pread_h(rentry->address + 6);
        opts.cflag  = pread_h(rentry->address + 8);
        opts.lflag  = pread_h(rentry->address + 10);
        opts.cerase = pread_b(rentry->address + 11);
        opts.ckill  = pread_b(rentry->address + 12);
        opts.cinter = pread_b(rentry->address + 13);
        opts.cquit  = pread_b(rentry->address + 14);
        opts.ceof   = pread_b(rentry->address + 15);
        opts.ceol   = pread_b(rentry->address + 16);
        opts.itime  = pread_b(rentry->address + 17);
        opts.vtime  = pread_b(rentry->address + 18);
        opts.vcount = pread_b(rentry->address + 19);

        sim_debug(TRACE_DBG, &ports_dev,  "    PPC Options: iflag=%04x\n", opts.iflag);
        sim_debug(TRACE_DBG, &ports_dev,  "    PPC Options: oflag=%04x\n", opts.oflag);
        sim_debug(TRACE_DBG, &ports_dev,  "    PPC Options: cflag=%04x\n", opts.cflag);
        sim_debug(TRACE_DBG, &ports_dev,  "    PPC Options: lflag=%04x\n", opts.lflag);
        sim_debug(TRACE_DBG, &ports_dev,  "    PPC Options: itime=%02x\n", opts.itime);
        sim_debug(TRACE_DBG, &ports_dev,  "    PPC Options: vtime=%02x\n", opts.vtime);
        sim_debug(TRACE_DBG, &ports_dev,  "    PPC Options: vcount=%02x\n", opts.vcount);

        ports_state[ln].iflag = opts.iflag;
        ports_state[ln].oflag = opts.oflag;

        if ((rentry->subdevice & 0xf) < PORTS_LINES) {
            /* Adjust baud rate */
            sprintf(line_config, "%s-8N1",
                    ports_baud[opts.cflag&0xf]);

            sim_debug(TRACE_DBG, &ports_dev,
                      "Setting PORTS line %d to %s\n",
                      ln, line_config);

            tmxr_set_config_line(&ports_ldsc[ln], line_config);
        }

        centry.byte_count = sizeof(PPC_OPTIONS);
        centry.opcode = PPC_OPTIONS;
        centry.subdevice = rentry->subdevice;
        centry.address = rentry->address;
        cio_cqueue(cid, CIO_STAT, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_OPTIONS);
        break;
    case PPC_VERS:
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] PPC Version\n",
                  R[NUM_PC]);

        /* Write the version number at the supplied address */
        pwrite_b(rentry->address, PORTS_VERSION);

        centry.opcode = CIO_ULM;

        /* TODO: It's unknown what the value 0x50 means, but this
         * is what a real board sends. */
        app_data[0] = 0x50;
        cio_cqueue(cid, CIO_STAT, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_VERS);
        break;
    case PPC_CONN:
        /* CONNECT - Full request and completion queues */
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] PPC CONNECT - subdevice = %02x\n",
                  R[NUM_PC], rentry->subdevice);

        ports_state[ln].conn = TRUE;

        centry.opcode = PPC_CONN;
        centry.subdevice = rentry->subdevice;
        centry.address = rentry->address;
        cio_cqueue(cid, CIO_STAT, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_CONN);
        break;
    case PPC_XMIT:
        /* XMIT - Full request and completion queues */

        /* The port being referred to is in the subdevice. */
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] PPC XMIT - subdevice = %02x, address=%08x, byte_count=%d\n",
                  R[NUM_PC], rentry->subdevice, rentry->address, rentry->byte_count);

        /* Set state for xmit */
        ports_state[ln].tx_addr = rentry->address;
        ports_state[ln].tx_req_addr = rentry->address;
        ports_state[ln].tx_chars = rentry->byte_count + 1;
        ports_state[ln].tx_req_chars = rentry->byte_count + 1;

        sim_activate_after(&ports_unit[1], ports_unit[1].wait);

        break;
    case PPC_DEVICE:
        /* DEVICE Control - Express request and completion queues */
        /* The port being referred to is in the subdevice. */
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] PPC DEVICE - subdevice = %02x\n",
                  R[NUM_PC], rentry->subdevice);
        centry.subdevice = rentry->subdevice;
        centry.opcode = PPC_DEVICE;
        cio_cexpress(cid, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_DEVICE);
        break;
    case PPC_RECV:
        /* RECV - Full request and completion queues */

        /* The port being referred to is in the subdevice. */
        sim_debug(TRACE_DBG, &ports_dev,
                  "[%08x] [ports_cmd] PPC RECV - subdevice = %02x addr=%08x\n",
                  R[NUM_PC], rentry->subdevice, rentry->address);

        break;
    case PPC_DISC:
        /* Disconnect */
        centry.subdevice = rentry->subdevice;
        centry.opcode = PPC_DISC;
        ports_ldsc[ln].rcve = 0;
        cio_cqueue(cid, CIO_STAT, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_STD);
        break;
    case PPC_BRK:
    case PPC_CLR:
    default:
        sim_debug(TRACE_DBG, &ports_dev,
                  ">>> Op %d Not Handled Yet\n",
                  rentry->opcode);

        cio_cexpress(cid, PPQESIZE, &centry, app_data);
        cio_irq(cid, rentry->subdevice, DELAY_STD);
        break;
    }
}

/*
 * Update the connection status of the given port.
 */
static void ports_update_conn(uint32 ln)
{
    cio_entry centry = {0};
    uint8 cid;
    uint8 app_data[4] = {0};

    cid = LCID(ln);

    /* If the card hasn't sysgened, there's no way to write a
     * completion queue entry */
    if (cio[cid].sysgen_s != CIO_SYSGEN) {
        return;
    }

    if (ports_ldsc[ln].conn) {
        app_data[0] = AC_CON;
        ports_state[ln].conn = TRUE;
    } else {
        if (ports_state[ln].conn) {
            app_data[0] = AC_DIS;
            ports_state[ln].conn = FALSE;
        } else {
            app_data[0] = 0;
        }
    }

    centry.opcode = PPC_ASYNC;
    centry.subdevice = LPORT(ln);
    cio_cqueue(cid, CIO_CMD, PPQESIZE, &centry, app_data);

    /* Interrupt */
    if (cio[cid].ivec > 0) {
        cio[cid].intr = TRUE;
    }
}

void ports_sysgen(uint8 cid)
{
    cio_entry cqe = {0};
    uint8 app_data[4] = {0};

    ports_crc = 0;

    cqe.opcode = 3; /* Sysgen success! */

    /* It's not clear why we put a response in both the express
     * and the full queue. */
    cio_cexpress(cid, PPQESIZE, &cqe, app_data);
    cio_cqueue(cid, CIO_STAT, PPQESIZE, &cqe, app_data);

    ports_int_cid = cid;
    sim_activate(&ports_unit[2], DELAY_STD);
}

void ports_express(uint8 cid)
{
    cio_entry rqe = {0};
    uint8 app_data[4] = {0};
    cio_rexpress(cid, PPQESIZE, &rqe, app_data);
    ports_cmd(cid, &rqe, app_data);
}

void ports_full(uint8 cid)
{
    uint32 i;
    cio_entry rqe = {0};
    uint8 app_data[4] = {0};

    for (i = 0; i < PORTS_LINES; i++) {
        if (cio_rqueue(cid, i, PPQESIZE, &rqe, app_data) == SCPE_OK) {
            ports_cmd(cid, &rqe, app_data);
        }
    }
}

t_stat ports_reset(DEVICE *dptr)
{
    int32 i;
    uint8 cid, line, ln, end_slot;
    TMLN *lp;

    ports_crc = 0;

    sim_debug(TRACE_DBG, &ports_dev,
              "[ports_reset] Resetting PORTS device\n");

    if ((dptr->flags & DEV_DIS)) {
        for (cid = 0; cid < CIO_SLOTS; cid++) {
            if (cio[cid].id == PORTS_ID) {
                cio[cid].id = 0;
                cio[cid].ipl = 0;
                cio[cid].ivec = 0;
                cio[cid].exp_handler = NULL;
                cio[cid].full_handler = NULL;
                cio[cid].sysgen = NULL;
            }
        }

        ports_conf = FALSE;
    } else if (!ports_conf) {

        /* Clear out any old cards, we're starting fresh */
        for (cid = 0; cid < CIO_SLOTS; cid++) {
            if (cio[cid].id == PORTS_ID) {
                cio[cid].id = 0;
                cio[cid].ipl = 0;
                cio[cid].ivec = 0;
                cio[cid].exp_handler = NULL;
                cio[cid].full_handler = NULL;
                cio[cid].sysgen = NULL;
            }
        }

        /* Find the first avaialable slot */
        for (cid = 0; cid < CIO_SLOTS; cid++) {
            if (cio[cid].id == 0) {
                break;
            }
        }

        /* Do we have room? */
        if (cid >= CIO_SLOTS || cid > (CIO_SLOTS - (ports_desc.lines/PORTS_LINES))) {
            return SCPE_NXM;
        }

        /* Remember the base card slot */
        ports_base_cid = cid;

        end_slot = (cid + (ports_desc.lines/PORTS_LINES));

        for (; cid < end_slot; cid++) {
            /* Set up the ports structure */
            cio[cid].id = PORTS_ID;
            cio[cid].ipl = PORTS_IPL;
            cio[cid].exp_handler = &ports_express;
            cio[cid].full_handler = &ports_full;
            cio[cid].sysgen = &ports_sysgen;

            for (line = 0; line < PORTS_LINES; line++) {
                ln = LN(cid, line);

                sim_debug(TRACE_DBG, &ports_dev,
                          ">>> Setting up lp %d (card %d, line %d)\n",
                          ln, cid, line);

                lp = &ports_ldsc[ln];
                tmxr_set_get_modem_bits(lp, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
            }
        }

        ports_conf = TRUE;

        if (ports_ldsc == NULL) {
            ports_desc.ldsc = ports_ldsc =
                (TMLN *)calloc(ports_desc.lines, sizeof(*ports_ldsc));
        }

        if (ports_state == NULL) {
            sim_debug(TRACE_DBG, &ports_dev,
                      "[ports_reset] calloc for ports_state...\n");
            ports_state = (PORTS_LINE_STATE *)calloc(ports_desc.lines, sizeof(*ports_state));
        }

        memset(ports_state, 0, ports_desc.lines*sizeof(*ports_state));

        tmxr_set_port_speed_control(&ports_desc);

        for (i = 0; i < ports_desc.lines; i++) {
            sim_debug(TRACE_DBG, &ports_dev,
                      "[ports_reset] Setting up line %d...\n", i);
            tmxr_set_line_unit(&ports_desc, i, &ports_unit[0]);
            tmxr_set_line_output_unit(&ports_desc, i, &ports_unit[1]);
            if (!ports_ldsc[i].conn) {
                ports_ldsc[i].xmte = 1;
            }
            ports_ldsc[i].rcve = 0;
            tmxr_set_config_line(&ports_ldsc[i], "9600-8N1");
        }
    }

    if (!sim_is_active(&ports_unit[0])) {
        sim_debug(TRACE_DBG, &ports_dev,
                  "[ports_reset] starting receive polling...\n");
        sim_activate(&ports_unit[0], ports_unit[0].wait);
    }

    sim_debug(TRACE_DBG, &ports_dev,
              "[ports_reset] returning scpe_ok\n");
    return SCPE_OK;
}

t_stat ports_cio_svc(UNIT *uptr)
{
    sim_debug(TRACE_DBG, &ports_dev,
              "[ports_cio_svc] IRQ for board %d device %d\n",
              ports_int_cid, ports_int_subdev);

    if (cio[ports_int_cid].ivec > 0) {
        cio[ports_int_cid].intr = TRUE;
    }

    switch (cio[ports_int_cid].op) {
    case PPC_CONN:
        cio[ports_int_cid].op = PPC_ASYNC;
        ports_ldsc[LN(ports_int_cid, ports_int_subdev)].rcve = 1;
        sim_activate(&ports_unit[2], DELAY_ASYNC);
        break;
    case PPC_ASYNC:
        ports_update_conn(LN(ports_int_cid, ports_int_subdev));
        break;
    default:
        break;
    }

    return SCPE_OK;
}

t_stat ports_rcv_svc(UNIT *uptr)
{
    uint8 cid;
    int32 temp, ln;
    char c;
    cio_entry rentry = {0};
    cio_entry centry = {0};
    uint8 rapp_data[4] = {0};
    uint8 capp_data[4] = {0};

    if ((uptr->flags & UNIT_ATT) == 0) {
        return SCPE_OK;
    }

    ln = tmxr_poll_conn(&ports_desc);
    if (ln >= 0) {
        ports_update_conn(ln);
    }

    tmxr_poll_rx(&ports_desc);

    for (ln = 0; ln < ports_desc.lines; ln++) {
        cid = LCID(ln);

        if (!ports_ldsc[ln].conn && ports_state[ln].conn) {
            ports_update_conn(ln);
        } else if (ports_ldsc[ln].conn && ports_state[ln].conn) {
            temp = tmxr_getc_ln(&ports_ldsc[ln]);

            if (temp && !(temp & SCPE_BREAK)) {

                c = (char) (temp & 0xff);

                sim_debug(IO_DBG, &ports_dev,
                          "[LINE %d RECEIVE] char = %02x (%c)\n",
                          ln, c, c);

                if (c == 0xd && (ports_state[ln].iflag & ICRNL)) {
                    c = 0xa;
                }

                if (cio[cid].ivec > 0 &&
                    cio_rqueue(cid, PORTS_RCV_QUEUE,
                               PPQESIZE, &rentry, rapp_data) == SCPE_OK) {
                    cio[cid].intr = TRUE;

                    /* Write the character to the memory address */
                    pwrite_b(rentry.address, c);
                    centry.subdevice = LPORT(ln);
                    centry.opcode = PPC_RECV;
                    centry.address = rentry.address;
                    capp_data[3] = RC_TMR;

                    cio_cqueue(cid, CIO_STAT, PPQESIZE, &centry, capp_data);
                }
            }
        }
    }

    tmxr_clock_coschedule(uptr, tmxr_poll);

    return SCPE_OK;
}

t_stat ports_xmt_svc(UNIT *uptr)
{
    uint8 cid, ln;
    char c;
    t_bool tx = FALSE; /* Did a tx ever occur? */
    cio_entry centry = {0};
    uint8 app_data[4] = {0};
    uint32 wait = 0x7fffffff;

    /* Scan all lines for output */
    for (ln = 0; ln < ports_desc.lines; ln++) {
        cid = LCID(ln);
        if (ports_ldsc[ln].conn && ports_state[ln].tx_chars > 0) {
            tx = TRUE; /* Even an attempt at TX counts for rescheduling */
            c = sim_tt_outcvt(pread_b(ports_state[ln].tx_addr),
                              TT_GET_MODE(ports_unit[0].flags));

            /* The PORTS card optionally handles NL->CRLF */
            if (c == 0xa &&
                (ports_state[ln].oflag & ONLCR) &&
                !(ports_state[ln].crlf)) {
                if (tmxr_putc_ln(&ports_ldsc[ln], 0xd) == SCPE_OK) {
                    wait = MIN(wait, ports_ldsc[ln].txdeltausecs);
                    sim_debug(IO_DBG, &ports_dev,
                              "[%08x] [ports_xmt_svc] [LINE %d] XMIT (crlf):  %02x (%c)\n",
                              R[NUM_PC], ln, 0xd, 0xd);
                    /* Indicate that we're in a CRLF translation */
                    ports_state[ln].crlf = TRUE;
                }

                break;
            }

            ports_state[ln].crlf = FALSE;

            if (tmxr_putc_ln(&ports_ldsc[ln], c) == SCPE_OK) {
                wait = MIN(wait, ports_ldsc[ln].txdeltausecs);
                ports_state[ln].tx_chars--;
                ports_state[ln].tx_addr++;
                sim_debug(IO_DBG, &ports_dev,
                          "[%08x] [ports_xmt_svc] [LINE %d] XMIT:         %02x (%c)\n",
                          R[NUM_PC], ln, c, c);
            }

            if (ports_state[ln].tx_chars == 0) {
                sim_debug(TRACE_DBG, &ports_dev,
                          "[%08x] [ports_xmt_svc] Done with xmit, card=%d port=%d. Interrupting.\n",
                          R[NUM_PC], cid, LPORT(ln));
                centry.byte_count = ports_state[ln].tx_req_chars;
                centry.subdevice = LPORT(ln);
                centry.opcode = PPC_XMIT;
                centry.address = ports_state[ln].tx_req_addr;
                app_data[0] = RC_FLU;
                cio_cqueue(cid, CIO_STAT, PPQESIZE, &centry, app_data);
                cio[cid].intr = TRUE;
            }
        }
    }

    tmxr_poll_tx(&ports_desc);

    if (tx) {
        tmxr_activate_after(uptr, wait);
    }

    return SCPE_OK;
}

t_stat ports_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    sim_debug(TRACE_DBG, &ports_dev, "ports_attach()\n");

    tmxr_set_modem_control_passthru(&ports_desc);

    r = tmxr_attach(&ports_desc, uptr, cptr);
    if (r != SCPE_OK) {
        tmxr_clear_modem_control_passthru(&ports_desc);
        return r;
    }

    return SCPE_OK;
}

t_stat ports_detach(UNIT *uptr)
{
    t_stat r;

    r = tmxr_detach(&ports_desc, uptr);

    if (r != SCPE_OK) {
        return r;
    }

    if (sim_is_active(&ports_unit[0])) {
        sim_debug(TRACE_DBG, &ports_dev,
                  "[ports_detach] Stopping receive polling...\n");
        sim_cancel(&ports_unit[0]);
    }

    tmxr_clear_modem_control_passthru(&ports_desc);

    return SCPE_OK;
}

/*
 * Useful routines for debugging request and completion queues
 */

t_stat ports_show_rqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    return ports_show_queue_common(st, uptr, val, desc, TRUE);
}

t_stat ports_show_cqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    return ports_show_queue_common(st, uptr, val, desc, FALSE);
}


static t_stat ports_show_queue_common(FILE *st, UNIT *uptr, int32 val,
                                      CONST void *desc, t_bool rq)
{
    uint8 cid;
    char *cptr = (char *) desc;
    t_stat result;
    uint32 ptr, size, no_rque, i, j;
    uint8  op, dev, seq, cmdstat;

    if (cptr) {
        cid = (uint8) get_uint(cptr, 10, 12, &result);
        if (result != SCPE_OK) {
            return SCPE_ARG;
        }
    } else {
        return SCPE_ARG;
    }

    /* If the card is not sysgen'ed, give up */
    if (cio[cid].sysgen_s != CIO_SYSGEN) {
        fprintf(st, "No card in slot %d, or card has not completed sysgen\n", cid);
        return SCPE_ARG;
    }

    /* Get the top of the queue */
    if (rq) {
        ptr = cio[cid].rqp;
        size = cio[cid].rqs;
        no_rque = cio[cid].no_rque;
    } else {
        ptr = cio[cid].cqp;
        size = cio[cid].cqs;
        no_rque = 0; /* Not used */
    }

    if (rq) {
        fprintf(st, "Dumping %d Request Queues\n", no_rque);
    } else {
        fprintf(st, "Dumping Completion Queue\n");
    }

    fprintf(st, "---------------------------------------------------------\n");
    fprintf(st, "EXPRESS ENTRY:\n");
    fprintf(st, "    Byte Count: %d\n",     pread_h(ptr));
    fprintf(st, "    Subdevice:  %d\n",     pread_b(ptr + 2));
    fprintf(st, "    Opcode:     0x%02x\n", pread_b(ptr + 3));
    fprintf(st, "    Addr/Data:  0x%08x\n", pread_w(ptr + 4));
    fprintf(st, "    App Data:   0x%08x\n", pread_w(ptr + 8));
    ptr += 12;

    if (rq) {
        for (i = 0; i < no_rque; i++) {
            fprintf(st, "---------------------------------------------------------\n");
            fprintf(st, "REQUEST QUEUE %d\n", i);
            fprintf(st, "---------------------------------------------------------\n");
            fprintf(st, "Load Pointer:   %d\n", pread_h(ptr) / 12);
            fprintf(st, "Unload Pointer: %d\n", pread_h(ptr + 2) / 12);
            fprintf(st, "---------------------------------------------------------\n");
            ptr += 4;
            for (j = 0; j < size; j++) {
                dev = pread_b(ptr + 2);
                op = pread_b(ptr + 3);
                seq = (dev & 0x40) >> 6;
                cmdstat = (dev & 0x80) >> 7;
                fprintf(st, "REQUEST ENTRY %d\n", j);
                fprintf(st, "    Byte Count: %d\n",          pread_h(ptr));
                fprintf(st, "    Subdevice:  %d\n",          dev & 0x3f);
                fprintf(st, "    Cmd/Stat:   %d\n",          cmdstat);
                fprintf(st, "    Seqbit:     %d\n",          seq);
                fprintf(st, "    Opcode:     0x%02x (%d)\n", op, op);
                fprintf(st, "    Addr/Data:  0x%08x\n",      pread_w(ptr + 4));
                fprintf(st, "    App Data:   0x%08x\n",      pread_w(ptr + 8));
                ptr += 12;
            }
        }
    } else {
        fprintf(st, "---------------------------------------------------------\n");
        fprintf(st, "Load Pointer:   %d\n", pread_h(ptr) / 12);
        fprintf(st, "Unload Pointer: %d\n", pread_h(ptr + 2) / 12);
        fprintf(st, "---------------------------------------------------------\n");
        ptr += 4;
        for (i = 0; i < size; i++) {
            dev = pread_b(ptr + 2);
            op = pread_b(ptr + 3);
            seq = (dev & 0x40) >> 6;
            cmdstat = (dev & 0x80) >> 7;
            fprintf(st, "COMPLETION ENTRY %d\n", i);
            fprintf(st, "    Byte Count: %d\n",          pread_h(ptr));
            fprintf(st, "    Subdevice:  %d\n",          dev & 0x3f);
            fprintf(st, "    Cmd/Stat:   %d\n",          cmdstat);
            fprintf(st, "    Seqbit:     %d\n",          seq);
            fprintf(st, "    Opcode:     0x%02x (%d)\n", op, op);
            fprintf(st, "    Addr/Data:  0x%08x\n",      pread_w(ptr + 4));
            fprintf(st, "    App Data:   0x%08x\n",      pread_w(ptr + 8));
            ptr += 12;
        }
    }

    return SCPE_OK;
}
