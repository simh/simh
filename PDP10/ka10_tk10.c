/* ka10_tk10.c: Knight kludge, TTY scanner.

   Copyright (c) 2018, Lars Brinkhoff

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

   This is a device with 16 terminal ports.  It's specific to the MIT
   AI lab and Dynamic Modeling PDP-10s.
*/

#include "sim_defs.h"
#include "sim_tmxr.h"
#include "kx10_defs.h"

#ifndef NUM_DEVS_TK10
#define NUM_DEVS_TK10 0
#endif

#if (NUM_DEVS_TK10 > 0)

#define TK10_NAME        "TK"
#define TK10_DEVNUM      0600  /* Also known as NTY. */
#define TK10_LINES       16

#define TK10_PIA         0000007 /* PI channel assignment */
#define TK10_RQINT       0000010 /* Request interrupt. */
#define TK10_ODONE       0000020 /* Done flag on typeout. */
#define TK10_STOP        0000020 /* Stop interrupting. */
#define TK10_IDONE       0000040 /* Done flag on input. */
#define TK10_TYI         0007400 /* Input TTY. */
#define TK10_TYO         0170000 /* Output TTY. */
#define TK10_INT         0200000 /* Interrupt. */
#define TK10_CLEAR       0200000 /* Clear interrupt. */
#define TK10_SELECT      0400000 /* Select line. */
#define TK10_GO          0 /* 0400000 Scanning. */

#define TK10_CONI_BITS   (TK10_PIA | TK10_INT | TK10_TYI | TK10_GO | \
                          TK10_ODONE | TK10_IDONE)

static t_stat       tk10_devio(uint32 dev, uint64 *data);
static t_stat       tk10_svc (UNIT *uptr);
static t_stat       tk10_reset (DEVICE *dptr);
static t_stat       tk10_attach (UNIT *uptr, CONST char *cptr);
static t_stat       tk10_detach (UNIT *uptr);
static const char   *tk10_description (DEVICE *dptr);
static t_stat       tk10_help (FILE *st, DEVICE *dptr, UNIT *uptr,
                               int32 flag, const char *cptr);
extern int32        tmxr_poll;

TMLN tk10_ldsc[TK10_LINES] = { 0 };
TMXR tk10_desc = { TK10_LINES, 0, 0, tk10_ldsc };

static uint64 status = 0;

UNIT                tk10_unit[] = {
    {UDATA(tk10_svc, TT_MODE_7B|UNIT_IDLE|UNIT_ATTABLE, 0)},  /* 0 */
};
DIB tk10_dib = {TK10_DEVNUM, 1, &tk10_devio, NULL};

MTAB tk10_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL, "7 bit mode" },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL, NULL, NULL, "8 bit mode" },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL, NULL, NULL, "7 bit mode - non printing suppressed" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &tk10_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &tk10_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &tk10_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &tk10_desc, "Display multiplexer statistics" },
    { 0 }
    };

DEVICE tk10_dev = {
    TK10_NAME, tk10_unit, NULL, tk10_mod,
    1, 8, 0, 1, 8, 36,
    NULL, NULL, tk10_reset, NULL, tk10_attach, tk10_detach,
    &tk10_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX, 0, dev_debug,
    NULL, NULL, tk10_help, NULL, NULL, tk10_description
};

static t_stat tk10_devio(uint32 dev, uint64 *data)
{
    TMLN *lp;
    int port;
    int ch;

    switch(dev & 07) {
    case CONO:
        sim_debug(DEBUG_CONO, &tk10_dev, "%06llo\n", *data);
        if (*data & TK10_CLEAR) {
            status &= ~TK10_INT;
            status |= TK10_GO;
            sim_debug(DEBUG_CMD, &tk10_dev, "Clear interrupt\n");
        }
        if (*data & TK10_STOP) {
            status &= ~TK10_ODONE;
            if (!(status & TK10_IDONE))
                status &= ~TK10_INT;
            sim_debug(DEBUG_CMD, &tk10_dev, "Clear output done port %lld\n",
                      (status & TK10_TYO) >> 12);
        }
        if (*data & TK10_RQINT) {
            status &= ~TK10_TYI;
            status |= ((status & TK10_TYO) >> 4) | TK10_ODONE | TK10_INT;
            sim_debug(DEBUG_CMD, &tk10_dev, "Request interrupt port %lld\n",
                      (status & TK10_TYO) >> 12);
        }
        if (*data & TK10_SELECT) {
            status &= ~TK10_TYO;
            status |= ((*data) & TK10_TYO);
            sim_debug(DEBUG_DETAIL, &tk10_dev, "Select port %lld\n",
                      (status & TK10_TYO) >> 12);
        }
        status &= ~TK10_PIA;
        status |= *data & TK10_PIA;
        break;
    case CONI:
        *data = status & TK10_CONI_BITS;
        sim_debug(DEBUG_CONI, &tk10_dev, "%06llo\n", *data);
        break;
    case DATAO:
        port = (status & TK10_TYO) >> 12;
        sim_debug(DEBUG_DATAIO, &tk10_dev, "DATAO port %d -> %012llo\n",
                  port, *data);
        if (tk10_ldsc[port].conn) {
            lp = &tk10_ldsc[port];
            ch = sim_tt_outcvt(*data & 0377, TT_GET_MODE (tk10_unit[0].flags));
            tmxr_putc_ln (lp, ch);
        }
        status &= ~TK10_ODONE;
        if (!(status & TK10_IDONE)) {
            status &= ~TK10_INT;
            status |= TK10_GO;
        }
        break;
    case DATAI:
        port = (status & TK10_TYO) >> 12;
        lp = &tk10_ldsc[port];
        *data = tmxr_getc_ln (lp);
        sim_debug(DEBUG_DATAIO, &tk10_dev, "DATAI port %d -> %012llo\n",
                  port, *data);
        status &= ~TK10_IDONE;
        if (!(status & TK10_ODONE)) {
            status &= ~TK10_INT;
            status |= TK10_GO;
        }
        break;
    }

    if (status & TK10_INT)
        set_interrupt(TK10_DEVNUM, status & TK10_PIA);
    else
        clr_interrupt(TK10_DEVNUM);

    return SCPE_OK;
}

static t_stat tk10_svc (UNIT *uptr)
{
    static int scan = 0;
    int i;

    /* Slow hardware only supported 300 baud teletypes. */
    sim_clock_coschedule (uptr, 2083);

    i = tmxr_poll_conn (&tk10_desc);
    if (i >= 0) {
        tk10_ldsc[i].conn = 1;
        tk10_ldsc[i].rcve = 1;
        tk10_ldsc[i].xmte = 1;
        sim_debug(DEBUG_CMD, &tk10_dev, "Connect %d\n", i);
    }

#if 0
    /* The GO bit is not yet properly modeled. */
    if (!(status & TK10_GO))
        return SCPE_OK;
#endif

    tmxr_poll_rx (&tk10_desc);
    tmxr_poll_tx (&tk10_desc);

    for (i = 0; i < TK10_LINES; i++) {
        /* Round robin scan 16 lines. */
        scan = (scan + 1) & 017;

        /* 1 means the line became ready since the last check.  Ignore
           -1 which means "still ready". */
        if (tmxr_txdone_ln (&tk10_ldsc[scan]) == 1) {
            sim_debug(DEBUG_DETAIL, &tk10_dev, "Output ready port %d\n", scan);
            status &= ~TK10_TYI;
            status |= scan << 8;
            status |= TK10_INT;
            status &= ~TK10_GO;
            status |= TK10_ODONE;
            set_interrupt(TK10_DEVNUM, status & TK10_PIA);
            break;
        }

        if (!tk10_ldsc[scan].conn)
            continue;

        if (tmxr_input_pending_ln (&tk10_ldsc[scan])) {
            sim_debug(DEBUG_DETAIL, &tk10_dev, "Input ready port %d\n", scan);
            status &= ~TK10_TYI;
            status |= scan << 8;
            status |= TK10_INT;
            status &= ~TK10_GO;
            status |= TK10_IDONE;
            set_interrupt(TK10_DEVNUM, status & TK10_PIA);
            break;
        }
    }

    return SCPE_OK;
}

static t_stat tk10_reset (DEVICE *dptr)
{
    int i;

    sim_debug(DEBUG_CMD, &tk10_dev, "Reset\n");
    if (tk10_unit->flags & UNIT_ATT)
        sim_activate (tk10_unit, tmxr_poll);
    else
        sim_cancel (tk10_unit);

    status = 0;
    clr_interrupt(TK10_DEVNUM);

    for (i = 0; i < TK10_LINES; i++) {
        tmxr_set_line_unit (&tk10_desc, i, tk10_unit);
        tmxr_set_line_output_unit (&tk10_desc, i, tk10_unit);
    }

    return SCPE_OK;
}

static t_stat tk10_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat stat;
    int i;

    stat = tmxr_attach (&tk10_desc, uptr, cptr);
    for (i = 0; i < TK10_LINES; i++) {
        tk10_ldsc[i].rcve = 0;
        tk10_ldsc[i].xmte = 0;
    }
    if (stat == SCPE_OK) {
        status = TK10_GO;
        sim_activate (uptr, tmxr_poll);
    }
    return stat;
}

static t_stat tk10_detach (UNIT *uptr)
{
    t_stat stat = tmxr_detach (&tk10_desc, uptr);
    int i;

    for (i = 0; i < TK10_LINES; i++) {
        tk10_ldsc[i].rcve = 0;
        tk10_ldsc[i].xmte = 0;
    }
    status = 0;
    sim_cancel (uptr);

    return stat;
}

static t_stat tk10_help (FILE *st, DEVICE *dptr, UNIT *uptr,
                         int32 flag, const char *cptr)
{
    fprintf (st, "TK10 Knight kludge TTY scanner\n\n");
    fprintf (st, "The TK10 supported 8 or 16 lines, but only the latter is supported by\n");
    fprintf (st, "this simulation.\n\n");
    fprintf (st, "The ATTACH command specifies the port to be used:\n\n");
    tmxr_attach_help (st, dptr, uptr, flag, cptr);
    fprintf (st, "Terminals can be set to one of three modes: 7P, 7B, or 8B.\n\n");
    fprintf (st, "  mode  input characters        output characters\n\n");
    fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
    fprintf (st, "                                non-printing characters suppressed\n");
    fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
    fprintf (st, "  8B    no changes              no changes\n\n");
    fprintf (st, "The default mode is 7B.\n\n");
    fprintf (st, "Once TK10 is attached and the simulator is running, the terminals listen for\n");
    fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
    fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
    fprintf (st, "by the Telnet client, a SET TK10 DISCONNECT command, or a DETACH TK10 command.\n\n");
    fprintf (st, "Other special commands:\n\n");
    fprintf (st, "   sim> SHOW TK10 CONNECTIONS    show current connections\n");
    fprintf (st, "   sim> SHOW TK10 STATISTICS     show statistics for active connections\n");
    fprintf (st, "   sim> SET TK10n DISCONNECT     disconnects the specified line.\n");
    fprint_reg_help (st, &dc_dev);
    fprintf (st, "\nThe terminals do not support save and restore.  All open connections\n");
    fprintf (st, "are lost when the simulator shuts down or TK10 is detached.\n");
    return SCPE_OK;
}


static const char *tk10_description (DEVICE *dptr)
{
    return "Knight kludge: TTY scanner";
}

#endif
