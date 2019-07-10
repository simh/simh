/* ka10_dpk.c: Systems Concepts DK-10, Datapoint kludge.

   Copyright (c) 2018-2019, Lars Brinkhoff

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   The Systems Concepts DK-10, also known as the Datapoint kludge, is
   a device with 16 terminal ports.  It's specific to the MIT AI lab
   PDP-10.
*/

#include <time.h>
#include "sim_defs.h"
#include "sim_tmxr.h"
#include "kx10_defs.h"

#define DPK_NAME        "DPK"
#define DPK_DEVNUM      0604
#define DPK_LINES       16

#define DPK_IEN          04000000 /* Interrupt enable. */

#define DPK_PIA         000000007 /* PI channel assignment */
#define DPK_IDONE       000000010 /* Input char available. */
#define DPK_NXM         000000020 /* NXM. */
#define DPK_PAR         000000040 /* Parity error. */
#define DPK_BUSY        000000100 /* Ouput line busy. */
#define DPK_IN          000000200 /* State of input line. */
#define DPK_ODONE       000000400 /* Output buffer done. */
#define DPK_OLINE       017000000 /* Line number, output. */

#define DPK_CONI_BITS   (DPK_PIA | DPK_IDONE | DPK_PAR | DPK_NXM | \
                          DPK_BUSY | DPK_IN | DPK_ODONE | DPK_OLINE)

#define DPK_FN           000000700 /* Function. */
#define DPK_SET_ODONE    000000000 /* Set output done. */
#define DPK_OSTART       000000100 /* Start output. */
#define DPK_ISTOP        000000200 /* Stop input. */
#define DPK_ISTART       000000300 /* Start input. */
#define DPK_OSTOP        000000400 /* Stop output, clear output done. */
#define DPK_OSPEED       000000500 /* Set output speed, start output. */
#define DPK_ISPEED_STOP  000000600 /* Set input speed, stop input. */
#define DPK_ISPEED_START 000000700 /* Set input speed, start input. */
#define DPK_SPEED        000007000 /* Speed code. */
#define DPK_ILINE        000170000 /* Line number. */
#define DPK_MANY         000200000 /* Apply to selected line through highest. */
#define DPK_RESET        000400000 /* Master clear. */

#define PORT_OUTPUT   1
#define PORT_INPUT    2

static t_stat       dpk_devio(uint32 dev, uint64 *data);
static t_stat       dpk_svc (UNIT *uptr);
static t_stat       dpk_reset (DEVICE *dptr);
static t_stat       dpk_attach (UNIT *uptr, CONST char *cptr);
static t_stat       dpk_detach (UNIT *uptr);
static const char   *dpk_description (DEVICE *dptr);
static t_stat       dpk_help (FILE *st, DEVICE *dptr, UNIT *uptr,
                               int32 flag, const char *cptr);
extern int32        tmxr_poll;

TMLN dpk_ldsc[DPK_LINES] = { 0 };
TMXR dpk_desc = { DPK_LINES, 0, 0, dpk_ldsc };

static int dpk_ien = 0;
static int dpk_base = 0;
static int dpk_status = 0;
static int dpk_port[16];
static int dpk_ibuf[16];
static int dpk_ird = 0;
static int dpk_iwr = 0;

UNIT                dpk_unit[] = {
    {UDATA(dpk_svc, TT_MODE_8B|UNIT_ATTABLE|UNIT_DISABLE, 0)},  /* 0 */
};
DIB dpk_dib = {DPK_DEVNUM, 1, &dpk_devio, NULL};

MTAB dpk_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL, "7 bit mode" },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL, NULL, NULL, "8 bit mode" },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL, NULL, NULL, "7 bit mode - non printing suppressed" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &dpk_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &dpk_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dpk_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dpk_desc, "Display multiplexer statistics" },
    { 0 }
    };

DEVICE dpk_dev = {
    DPK_NAME, dpk_unit, NULL, dpk_mod,
    1, 8, 0, 1, 8, 36,
    NULL, NULL, dpk_reset, NULL, dpk_attach, dpk_detach,
    &dpk_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, dpk_help, NULL, NULL, dpk_description
};

static t_stat dpk_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &dpk_dev;
    int port;

    switch(dev & 07) {
    case CONO|4:
        sim_debug(DEBUG_CONO, &dpk_dev, "%012llo\n", *data);
        port = (*data & DPK_ILINE) >> 12;
        if (*data & DPK_RESET)
            dpk_reset (&dpk_dev);
        if (*data & DPK_IDONE) {
            dpk_status &= ~DPK_IDONE;
            dpk_iwr = dpk_ird = 0;
        }
        if (*data & DPK_PAR)
            dpk_status &= ~DPK_PAR;
        if (*data & DPK_NXM)
            dpk_status &= ~DPK_NXM;
        switch (*data & DPK_FN) {
        case DPK_SET_ODONE:
            dpk_status |= DPK_ODONE;
            break;
        case DPK_OSTART:
            dpk_port[port] |= PORT_OUTPUT;
            dpk_status &= ~DPK_ODONE;
            break;
        case DPK_ISTOP:
            dpk_port[port] &= ~PORT_INPUT;
            break;
        case DPK_ISTART:
            dpk_port[port] |= PORT_OUTPUT;
            break;
        case DPK_OSTOP:
            dpk_port[port] &= ~PORT_OUTPUT;
            dpk_status &= ~DPK_ODONE;
            break;
        case DPK_OSPEED:
            sim_debug(DEBUG_CMD, &dpk_dev, "Set port %d output speed %lld\n",
                      port, (*data & DPK_SPEED) >> 9);
            dpk_port[port] |= PORT_OUTPUT;
            break;
        case DPK_ISPEED_STOP:
            dpk_port[port] &= ~PORT_INPUT;
            goto ispeed;
        case DPK_ISPEED_START:
            dpk_port[port] |= PORT_INPUT;
        ispeed:
            sim_debug(DEBUG_CMD, &dpk_dev, "Set port %d input speed %lld\n",
                      port, (*data & DPK_SPEED) >> 9);
            break;
        default:
            fprintf (stderr, "Unknown function: %llo\n", *data);
            exit (1);
            break;
        }
        dpk_status &= ~DPK_PIA;
        dpk_status |= *data & DPK_PIA;
        break;
    case CONI|4:
        *data = dpk_status & DPK_CONI_BITS;
        sim_debug(DEBUG_CONI, &dpk_dev, "%07llo\n", *data);
        break;
    case DATAO|4:
        dpk_base = *data & 03777777;
        if (*data & DPK_IEN)
          dpk_ien = 1;
        sim_debug(DEBUG_DATAIO, &dpk_dev, "DATAO %06llo\n", *data);
        break;
    case DATAI|4:
        if (dpk_ird == dpk_iwr) {
            *data = 0;
            break;
        }
        *data = dpk_ibuf[dpk_ird++];
        dpk_ird &= 15;
        sim_debug(DEBUG_DATAIO, &dpk_dev, "DATAI %06llo\n", *data);
        if (dpk_ird == dpk_iwr) {
            dpk_status &= ~DPK_IDONE;
        }
        break;
    }

    if (dpk_ien && (dpk_status & (DPK_IDONE | DPK_ODONE)))
        set_interrupt(DPK_DEVNUM, dpk_status & DPK_PIA);
    else
        clr_interrupt(DPK_DEVNUM);

    return SCPE_OK;
}

/* Note, the byte pointer used by the hardware has halfwords swapped. */
static int ildb (uint64 *pointer)
{
    uint64 bp = *pointer;
    int pos, ch, addr;

 again:
    pos = (bp >> 12) & 077;
    pos -= 7;
    addr = (bp >> 18) & 0777777;
    if (pos < 0) {
        pos = 36 - 7;
        addr++;
        addr &= 0777777;
    }

    if (M[addr] & 1) {
        bp = M[addr];
        goto again;
    } else
        *pointer = ((uint64)addr << 18) | (pos << 12) | 7 << 6;

    ch = (M[addr] >> pos) & 0177;
    return ch;
}

static int dpk_output (int port, TMLN *lp)
{
    uint64 count;
    int ch;

    if ((dpk_port[port] & PORT_OUTPUT) == 0)
        return 0;

    if (M[dpk_base + 2*port] == 0777777777777LL) {
        dpk_port[port] &= ~PORT_OUTPUT;
        dpk_status &= ~DPK_OLINE;
        dpk_status |= port << 18;
        dpk_status |= DPK_ODONE;
        if (dpk_ien)
            set_interrupt(DPK_DEVNUM, dpk_status & DPK_PIA);
        return 0;
    }

    ch = ildb (&M[dpk_base + 2*port + 1]);
    ch = sim_tt_outcvt(ch & 0377, TT_GET_MODE (dpk_unit[0].flags));
    tmxr_putc_ln (lp, ch);
            
    count = M[dpk_base + 2*port] - 1;
    M[dpk_base + 2*port] = count & 0777777777777LL;

    return 1;
}

static t_stat dpk_svc (UNIT *uptr)
{
    static int scan = 0;
    int i;

    /* 16 ports at 4800 baud, rounded up. */
    sim_activate_after (uptr, 200);

    i = tmxr_poll_conn (&dpk_desc);
    if (i >= 0) {
        dpk_ldsc[i].conn = 1;
        dpk_ldsc[i].rcve = 1;
        dpk_ldsc[i].xmte = 1;
        sim_debug(DEBUG_CMD, &dpk_dev, "Connect %d\n", i);
    }

    tmxr_poll_rx (&dpk_desc);
    tmxr_poll_tx (&dpk_desc);

    for (i = 0; i < DPK_LINES; i++) {
        /* Round robin scan 16 lines. */
        scan = (scan + 1) & 017;

        /* 1 means the line became ready since the last check.  Ignore
           -1 which means "still ready". */
        if (tmxr_txdone_ln (&dpk_ldsc[scan])) {
            if (dpk_output (scan, &dpk_ldsc[scan]))
                break;
        }

        if (!dpk_ldsc[scan].conn)
            continue;

        if (tmxr_input_pending_ln (&dpk_ldsc[scan])) {
            if ((dpk_port[scan] & PORT_INPUT) == 0)
                continue;
            dpk_ibuf[dpk_iwr++] = (scan << 18) | (tmxr_getc_ln (&dpk_ldsc[scan]) & 0177);
            dpk_iwr &= 15;
            dpk_status |= DPK_IDONE;
            if (dpk_ien)
               set_interrupt(DPK_DEVNUM, dpk_status & DPK_PIA);
            break;
        }
    }

    return SCPE_OK;
}

static t_stat dpk_reset (DEVICE *dptr)
{
    sim_debug(DEBUG_CMD, &dpk_dev, "Reset\n");
    if (dpk_unit->flags & UNIT_ATT)
        sim_activate (dpk_unit, tmxr_poll);
    else
        sim_cancel (dpk_unit);

    dpk_ien = 0;
    dpk_base = 0;
    dpk_status = 0;
    dpk_ird = dpk_iwr = 0;
    memset (dpk_port, 0, sizeof dpk_port);
    clr_interrupt(DPK_DEVNUM);

    return SCPE_OK;
}

static t_stat dpk_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat stat;
    int i;

    stat = tmxr_attach (&dpk_desc, uptr, cptr);
    for (i = 0; i < DPK_LINES; i++) {
        dpk_ldsc[i].rcve = 0;
        dpk_ldsc[i].xmte = 0;
        /* Clear txdone so tmxr_txdone_ln will not return return true
           on the first call. */
        dpk_ldsc[i].txdone = 0;
    }
    if (stat == SCPE_OK)
        sim_activate (uptr, tmxr_poll);
    return stat;
}

static t_stat dpk_detach (UNIT *uptr)
{
    t_stat stat = tmxr_detach (&dpk_desc, uptr);
    int i;

    for (i = 0; i < DPK_LINES; i++) {
        dpk_ldsc[i].rcve = 0;
        dpk_ldsc[i].xmte = 0;
    }
    dpk_status = 0;
    sim_cancel (uptr);

    return stat;
}

static t_stat dpk_help (FILE *st, DEVICE *dptr, UNIT *uptr,
                         int32 flag, const char *cptr)
{
    fprintf (st, "DPK Datapoint kludge terminal multiplexer\n\n");
    fprintf (st, "The ATTACH command specifies the port to be used:\n\n");
    tmxr_attach_help (st, dptr, uptr, flag, cptr);
    fprintf (st, "Terminals can be set to one of three modes: 7P, 7B, or 8B.\n\n");
    fprintf (st, "  mode  input characters        output characters\n\n");
    fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
    fprintf (st, "                                non-printing characters suppressed\n");
    fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
    fprintf (st, "  8B    no changes              no changes\n\n");
    fprintf (st, "The default mode is 7B.\n\n");
    fprintf (st, "Once DPK is attached and the simulator is running, the terminals listen for\n");
    fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
    fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
    fprintf (st, "by the Telnet client, a SET DPK DISCONNECT command, or a DETACH DPK command.\n\n");
    fprintf (st, "Other special commands:\n\n");
    fprintf (st, "   sim> SHOW DPK CONNECTIONS    show current connections\n");
    fprintf (st, "   sim> SHOW DPK STATISTICS     show statistics for active connections\n");
    fprintf (st, "   sim> SET DPKn DISCONNECT     disconnects the specified line.\n");
    fprint_reg_help (st, &dc_dev);
    fprintf (st, "\nThe terminals do not support save and restore.  All open connections\n");
    fprintf (st, "are lost when the simulator shuts down or DPK is detached.\n");
    return SCPE_OK;
}


static const char *dpk_description (DEVICE *dptr)
{
    return "Systems Concepts DK-10, Datapoint kludge";
}
