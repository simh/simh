/* ka10_tk10.c: MTY, Morton multiplex box: Terminal multiplexor.

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

   This is a device with 32 high-speed terminal lines.  It's specific
   to the MIT Mathlab and Dynamic Modeling PDP-10s.
*/

#include <time.h>
#include "sim_defs.h"
#include "sim_tmxr.h"
#include "kx10_defs.h"

#ifndef NUM_DEVS_MTY
#define NUM_DEVS_MTY 0
#endif

#if (NUM_DEVS_MTY > 0)

#define MTY_NAME        "MTY"
#define MTY_DEVNUM      0400
#define MTY_LINES       32
#define MTY_FIRST       C1      /* Frist character in output word. */

#define MTY_PIA         0000007 /* PI channel assignment */
#define MTY_RQINT       0000010 /* Request interrupt. */
#define MTY_ODONE       0000010 /* Output done. */
#define MTY_IDONE       0000040 /* Input done. */
#define MTY_STOP        0000200 /* Clear output done. */
#define MTY_LINE        0370000 /* Line number. */

#define MTY_DONE        (MTY_IDONE | MTY_ODONE)
#define MTY_CONI_BITS   (MTY_PIA | MTY_DONE | MTY_LINE)
#define MTY_CONO_BITS   (MTY_PIA | MTY_LINE)

static t_stat       mty_devio(uint32 dev, uint64 *data);
static t_stat       mty_input_svc (UNIT *uptr);
static t_stat       mty_output_svc (UNIT *uptr);
static t_stat       mty_reset (DEVICE *dptr);
static t_stat       mty_attach (UNIT *uptr, CONST char *cptr);
static t_stat       mty_detach (UNIT *uptr);
static const char   *mty_description (DEVICE *dptr);
static t_stat       mty_help (FILE *st, DEVICE *dptr, UNIT *uptr,
                               int32 flag, const char *cptr);
extern int32        tmxr_poll;

static uint32       mty_active_bitmask;
static uint64       mty_output_word[MTY_LINES];
static int32        mty_input_character;

TMLN mty_ldsc[MTY_LINES] = { 0 };
TMXR mty_desc = { MTY_LINES, 0, 0, mty_ldsc };

static uint64 status = 0;

UNIT                mty_unit[] = {
    {UDATA(mty_input_svc, TT_MODE_7B|UNIT_IDLE|UNIT_ATTABLE, 0)},  /* 0 */
    {UDATA(mty_output_svc, UNIT_DIS|UNIT_IDLE, 0)},  /* 0 */
};
DIB mty_dib = {MTY_DEVNUM, 1, &mty_devio, NULL};

MTAB mty_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL, "7 bit mode" },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL, NULL, NULL, "7 bit mode - non printing suppressed" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &mty_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &mty_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &mty_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &mty_desc, "Display multiplexer statistics" },
    { 0 }
    };

DEVICE mty_dev = {
    MTY_NAME, mty_unit, NULL, mty_mod,
    2, 8, 0, 1, 8, 36,
    NULL, NULL, mty_reset, NULL, mty_attach, mty_detach,
    &mty_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, mty_help, NULL, NULL, mty_description
};

static t_stat mty_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &mty_dev;
    TMLN *lp;
    int line;
    uint64 word;

    switch(dev & 07) {
    case CONO:
        sim_debug(DEBUG_CONO, &mty_dev, "%06llo\n", *data);
        status &= ~MTY_CONO_BITS;
        status |= *data & MTY_CONO_BITS;
        line = (status & MTY_LINE) >> 12;
        if (*data & MTY_STOP) {
            status &= ~MTY_ODONE;
            sim_debug(DEBUG_CMD, &mty_dev, "Clear output done line %d\n",
                      line);
        }
        if (*data & MTY_RQINT) {
            status |= MTY_ODONE;
            sim_debug(DEBUG_CMD, &mty_dev, "Request interrupt line %d\n",
                      line);
        }
        if ((*data & (MTY_STOP | MTY_RQINT)) == 0)
            sim_debug(DEBUG_CMD, &mty_dev, "Select line %d\n",
                      line);
        break;
    case CONI:
        *data = status & MTY_CONI_BITS;
        sim_debug(DEBUG_CONI, &mty_dev, "%06llo\n", *data);
        break;
    case DATAO:
        line = (status & MTY_LINE) >> 12;
        word = *data;
        sim_debug(DEBUG_DATAIO, &mty_dev, "DATAO line %d -> %012llo\n",
                  line, word);
        lp = &mty_ldsc[line];
        mty_output_word[line] = word | MTY_FIRST;
        mty_active_bitmask |= 1 << line;
        sim_activate_abs (&mty_unit[1], 0);
        status &= ~MTY_ODONE;
        break;
    case DATAI:
        line = (status & MTY_LINE) >> 12;
        lp = &mty_ldsc[line];
        *data = mty_input_character;
        sim_debug(DEBUG_DATAIO, &mty_dev, "DATAI line %d -> %012llo\n",
                  line, *data);
        status &= ~MTY_IDONE;
        sim_activate_abs (&mty_unit[0], 0);
        break;
    }

    if (status & MTY_DONE)
        set_interrupt(MTY_DEVNUM, status & MTY_PIA);
    else
        clr_interrupt(MTY_DEVNUM);

    return SCPE_OK;
}

static t_stat mty_input_svc (UNIT *uptr)
{
    static int scan = 0;
    int32 ch;
    int i;

    sim_clock_coschedule (uptr, 1000);

    i = tmxr_poll_conn (&mty_desc);
    if (i >= 0) {
        mty_ldsc[i].rcve = 1;
        mty_ldsc[i].xmte = 1;
        sim_debug(DEBUG_CMD, &mty_dev, "Connect %d\n", i);
    }

    tmxr_poll_rx (&mty_desc);

    for (i = 0; i < MTY_LINES; i++) {
        /* Round robin scan 32 lines. */
        scan = (scan + 1) & 037;

        ch = tmxr_getc_ln (&mty_ldsc[scan]);
        if (ch & TMXR_VALID) {
            mty_input_character = ch & 0177;
            sim_debug(DEBUG_DETAIL, &mty_dev, "Input ready line %d\n", scan);
            status &= ~MTY_LINE;
            status |= scan << 12;
            status |= MTY_IDONE;
            set_interrupt(MTY_DEVNUM, status & MTY_PIA);

            /* No more scanning until DATAI has read this character. */
            sim_cancel (&mty_unit[0]);
            break;
        }
    }

    return SCPE_OK;
}

static t_stat mty_output_svc (UNIT *uptr)
{
    static int scan = 0;
    uint64 word;
    int i, ch;
    int32 txdone;

    for (i = 0; i < MTY_LINES; i++) {
        /* Round robin scan 32 lines. */
        scan = (scan + 1) & 037;

        if ((mty_active_bitmask & (1 << scan)) == 0 ||
            (txdone = tmxr_txdone_ln (&mty_ldsc[scan])) == 0)
            continue;

        /* Write up to five characters extracted from a word.  NUL
           can only be in the first character. */
        word = mty_output_word[scan];
        if (word != 0) {
            ch = (word >> 29) & 0177;
            ch = sim_tt_outcvt(ch, TT_GET_MODE (mty_unit[0].flags));
            if (tmxr_putc_ln (&mty_ldsc[scan], ch) != SCPE_STALL)
                mty_output_word[scan] = (word << 7) & FMASK;
        } else if (txdone == 1) {
            sim_debug(DEBUG_DETAIL, &mty_dev, "Output ready line %d\n", scan);
            status &= ~MTY_LINE;
            status |= scan << 12;
            status |= MTY_ODONE;
            set_interrupt(MTY_DEVNUM, status & MTY_PIA);
            mty_active_bitmask &= ~(1 << scan);

            /* Stop scanning; can only signal output done for one line
               at a time. */
            break;
        }
    }

    tmxr_poll_tx (&mty_desc);

    /* SIMH will actually schedule this UNIT when output is due
       according to the line speed. */
    sim_activate_after (uptr, 1000000);

    return SCPE_OK;
}

static t_stat mty_reset (DEVICE *dptr)
{
    int i;

    sim_debug(DEBUG_CMD, &mty_dev, "Reset\n");
    if (mty_unit->flags & UNIT_ATT) {
        sim_activate (mty_unit, tmxr_poll);
        sim_activate_after (&mty_unit[1], 100);
    } else {
        sim_cancel (&mty_unit[0]);
        sim_cancel (&mty_unit[1]);
    }

    status = 0;
    clr_interrupt(MTY_DEVNUM);

    for (i = 0; i < MTY_LINES; i++) {
        tmxr_set_line_unit (&mty_desc, i, &mty_unit[0]);
        tmxr_set_line_output_unit (&mty_desc, i, &mty_unit[1]);
        tmxr_set_line_speed(&mty_ldsc[i], "80000");
    }

    return SCPE_OK;
}

static t_stat mty_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat stat;
    int i;

    stat = tmxr_attach (&mty_desc, uptr, cptr);
    for (i = 0; i < MTY_LINES; i++) {
        mty_ldsc[i].rcve = 0;
        mty_ldsc[i].xmte = 0;
    }
    if (stat == SCPE_OK) {
        status = 0;
        sim_activate (uptr, tmxr_poll);
    }
    mty_active_bitmask = 0;
    return stat;
}

static t_stat mty_detach (UNIT *uptr)
{
    t_stat stat = tmxr_detach (&mty_desc, uptr);
    int i;

    for (i = 0; i < MTY_LINES; i++) {
        mty_ldsc[i].rcve = 0;
        mty_ldsc[i].xmte = 0;
    }
    status = 0;
    sim_cancel (&mty_unit[0]);
    sim_cancel (&mty_unit[1]);

    return stat;
}

static t_stat mty_help (FILE *st, DEVICE *dptr, UNIT *uptr,
                         int32 flag, const char *cptr)
{
    fprintf (st, "MTY Morton box terminal multiplexor\n\n");
    fprintf (st, "The MTY supported 32 high-speed lines at up to 80 bits/second.\n");
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
    fprintf (st, "Once MTY is attached and the simulator is running, the terminals listen for\n");
    fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
    fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
    fprintf (st, "by the Telnet client, a SET MTY DISCONNECT command, or a DETACH MTY command.\n\n");
    fprintf (st, "Other special commands:\n\n");
    fprintf (st, "   sim> SHOW MTY CONNECTIONS    show current connections\n");
    fprintf (st, "   sim> SHOW MTY STATISTICS     show statistics for active connections\n");
    fprintf (st, "   sim> SET MTYn DISCONNECT     disconnects the specified line.\n");
    fprint_reg_help (st, &dc_dev);
    fprintf (st, "\nThe terminals do not support save and restore.  All open connections\n");
    fprintf (st, "are lost when the simulator shuts down or MTY is detached.\n");
    return SCPE_OK;
}


static const char *mty_description (DEVICE *dptr)
{
    return "Morton box: Terminal multiplexor";
}
#endif
