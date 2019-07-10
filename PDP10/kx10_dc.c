/* ka10_dc.c: PDP-10 DC10 communication server simulator

   Copyright (c) 2011-2017, Richard Cornwell

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

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell.

*/

#include "kx10_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_DC
#define NUM_DEVS_DC 0
#endif

#if (NUM_DEVS_DC > 0)

#define DC_DEVNUM 0240

#define DC10_LINES    8
#define DC10_MLINES   32

#define STATUS   u3

#define DTS_LINE 007700         /* Scanner line number in STATUS */
#define PI_CHN   000007         /* IN STATUS. */
#define RCV_PI   000010         /* IN STATUS. */
#define XMT_PI   000020         /* IN STATUS. */
#define DTR_DIS  000040         /* DTR FLAG */
#define RST_SCN  000010         /* CONO */
#define DTR_SET  000020         /* CONO */
#define CLR_SCN  000040         /* CONO */

#define DATA     0000377
#define FLAG     0000400        /* Recieve data/ transmit disable */
#define LINE     0000077        /* Line number in Left */
#define LFLAG    0000100        /* Direct line number flag */

/* DC10E flags */
#define CTS      0000004        /* Clear to send */
#define RES_DET  0000002        /* Ring detect */
#define DLO      0000040        /* (ACU) Data line occupied */
#define PND      0000020        /* (ACU) Present next digit */
#define ACR      0000010        /* (ACU) Abandon Call and retry */
#define CRQ      0000040        /* (ACU) Call Request */
#define DPR      0000020        /* (ACU) Digit Presented */
#define NB       0000017        /* (ACU) Number */
#define OFF_HOOK 0000100        /* Off Hook (CD) */
#define CAUSE_PI 0000200        /* Cause PI */

uint64   dc_l_status;                             /* Line status */
int      dc_l_count = 0;                          /* Scan counter */
int      dc_modem = DC10_MLINES;                  /* Modem base address */
uint8    dcix_buf[DC10_MLINES] = { 0 };           /* Input buffers */
uint8    dcox_buf[DC10_MLINES] = { 0 };           /* Output buffers */
TMLN     dc_ldsc[DC10_MLINES] = { 0 };            /* Line descriptors */
TMXR     dc_desc = { DC10_LINES, 0, 0, dc_ldsc };
uint32   tx_enable, rx_rdy;                       /* Flags */
uint32   dc_enable;                               /* Enable line */
uint32   dc_ring;                                 /* Connection pending */
uint32   rx_conn;                                 /* Connection flags */
extern int32 tmxr_poll;

t_stat dc_devio(uint32 dev, uint64 *data);
t_stat dc_svc (UNIT *uptr);
t_stat dc_doscan (UNIT *uptr);
t_stat dc_reset (DEVICE *dptr);
t_stat dc_set_modem (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dc_show_modem (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dc_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dc_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dc_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dc_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dc_attach (UNIT *uptr, CONST char *cptr);
t_stat dc_detach (UNIT *uptr);
t_stat dc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr);
const char *dc_description (DEVICE *dptr);

/* DC10 data structures

   dc_dev      DC10 device descriptor
   dc_unit     DC10 unit descriptor
   dc_reg      DC10 register list
*/

DIB dc_dib = { DC_DEVNUM, 1, &dc_devio, NULL };

UNIT dc_unit = {
    UDATA (&dc_svc, TT_MODE_7B+UNIT_IDLE+UNIT_ATTABLE, 0), KBD_POLL_WAIT
    };

REG dc_reg[] = {
    { DRDATA (TIME, dc_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (STATUS, dc_unit.STATUS, 18), PV_LEFT },
    { NULL }
    };

MTAB dc_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", NULL },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  NULL },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  NULL },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &dc_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &dc_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dc_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dc_desc, "Display multiplexer statistics" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dc_setnl, &tmxr_show_lines, (void *) &dc_desc, "Set number of lines" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "MODEM", "MODEM=n",
        &dc_set_modem, &dc_show_modem, (void *)&dc_modem, "Set modem offset" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "LOG=n=file",
        &dc_set_log, NULL, (void *)&dc_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "NOLOG",
        &dc_set_nolog, NULL, (void *)&dc_desc, "Disable logging on designated line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "LOG", NULL,
        NULL, &dc_show_log, (void *)&dc_desc, "Display logging for all lines" },
    { 0 }
    };

DEVICE dc_dev = {
    "DC", &dc_unit, dc_reg, dc_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &dc_reset,
    NULL, &dc_attach, &dc_detach,
    &dc_dib, DEV_NET | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dc_help, NULL, NULL, &dc_description
    };



/* IOT routine */
t_stat dc_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &dc_unit;
    TMLN *lp;
    int   ln;

    switch(dev & 3) {
    case CONI:
         /* Check if we might have any interrupts pending */
         if ((uptr->STATUS & (RCV_PI|XMT_PI)) == 0)
             dc_doscan(uptr);
         *data = uptr->STATUS & (PI_CHN|RCV_PI|XMT_PI);
         sim_debug(DEBUG_CONI, &dc_dev, "DC %03o CONI %06o PC=%o\n",
               dev, (uint32)*data, PC);
         break;

    case CONO:
         /* Set PI */
         uptr->STATUS &= ~PI_CHN;
         uptr->STATUS |= PI_CHN & *data;
         if (*data & RST_SCN)
             dc_l_count = 0;
         if (*data & DTR_SET)
             uptr->STATUS |= DTR_SET;
         if (*data & CLR_SCN) {
             uptr->STATUS &= PI_CHN;
             for (ln = 0; ln < dc_desc.lines; ln++) {
                lp = &dc_ldsc[ln];
                if (lp->conn) {
                    tmxr_linemsg (lp, "\r\nLine Hangup\r\n");
                    tmxr_reset_ln(lp);
                }
             }
             tx_enable = 0;
             dc_enable = 0;
             rx_rdy = 0;                                /* Flags */
             rx_conn = 0;
             dc_ring = 0;
             dc_l_status = 0;
         }

         sim_debug(DEBUG_CONO, &dc_dev, "DC %03o CONO %06o PC=%06o\n",
               dev, (uint32)*data, PC);
         dc_doscan(uptr);
         break;

    case DATAO:
         if (*data & (LFLAG << 18))
             ln = (*data >> 18) & 077;
         else
             ln = dc_l_count;
         if (ln >= dc_modem) {
             if (*data & CAUSE_PI)
                dc_l_status |= (1LL << ln);
             else
                dc_l_status &= ~(1LL << ln);
             ln -= dc_modem;
             sim_debug(DEBUG_DETAIL, &dc_dev, "DC line modem %d %03o\n",
                   ln, (uint32)(*data & 0777));
             if ((*data & OFF_HOOK) == 0) {
                uint32 mask = ~(1 << ln);
                rx_rdy &= mask;
                tx_enable &= mask;
                dc_enable &= mask;
                lp = &dc_ldsc[ln];
                if (rx_conn & (1 << ln) && lp->conn) {
                    sim_debug(DEBUG_DETAIL, &dc_dev, "DC line hangup %d\n", ln);
                    tmxr_linemsg (lp, "\r\nLine Hangup\r\n");
                    tmxr_reset_ln(lp);
                    rx_conn &= mask;
                }
             } else {
                sim_debug(DEBUG_DETAIL, &dc_dev, "DC line off-hook %d\n", ln);
                dc_enable |= 1<<ln;
                if (dc_ring & (1 << ln)) {
                    dc_l_status |= (1LL << (ln + dc_modem));
                    dc_ring &= ~(1 << ln);
                    rx_conn |= (1 << ln);
                }
             }
         } else if (ln < dc_desc.lines) {
             lp = &dc_ldsc[ln];
             if (*data & FLAG) {
                tx_enable &= ~(1 << ln);
                dc_l_status &= ~(1LL << ln);
             } else if (lp->conn) {
                int32 ch = *data & DATA;
                ch = sim_tt_outcvt(ch, TT_GET_MODE (dc_unit.flags) | TTUF_KSR);
                tmxr_putc_ln (lp, ch);
                if (lp->xmte)
                    tx_enable |= (1 << ln);
                else
                    tx_enable &= ~(1 << ln);
                dc_l_status |= (1LL << ln);
             }
         }
         dc_doscan(uptr);
         sim_debug(DEBUG_DATAIO, &dc_dev, "DC %03o DATO %012llo PC=%06o\n",
                    dev, *data, PC);
         break;

    case DATAI:
         ln = dc_l_count;
         *data = (uint64)(ln) << 18;
         if (ln >= dc_modem) {
             dc_l_status &= ~(1LL << ln);
             ln = ln - dc_modem;
             lp = &dc_ldsc[ln];
             if (dc_enable & (1 << ln))
                *data |= FLAG|OFF_HOOK;
             if (rx_conn & (1 << ln) && lp->conn)
                *data |= FLAG|CTS;
             if (dc_ring & (1 << ln))
                *data |= FLAG|RES_DET;
         } else if (ln < dc_desc.lines) {
             /* Nothing happens if no recieve data, which is transmit ready */
             lp = &dc_ldsc[ln];
             if (tmxr_rqln (lp) > 0) {
                int32 ch = tmxr_getc_ln (lp);
                if (ch & SCPE_BREAK)                      /* break? */
                    ch = 0;
                else
                    ch = sim_tt_inpcvt (ch, TT_GET_MODE(dc_unit.flags) | TTUF_KSR);
                *data |= FLAG | (uint64)(ch & DATA);
             }
             if (tmxr_rqln (lp) > 0) {
                rx_rdy |= 1 << ln;
                dc_l_status |= (1LL << ln);
             } else {
                rx_rdy &= ~(1 << ln);
                dc_l_status &= ~(1LL << ln);
             }
         }
         dc_doscan(uptr);
         sim_debug(DEBUG_DATAIO, &dc_dev, "DC %03o DATI %012llo PC=%06o\n",
                    dev, *data, PC);
         break;
    }
    return SCPE_OK;
}


/* Unit service */

t_stat dc_svc (UNIT *uptr)
{
int32 ln;

    if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
        return SCPE_OK;
    ln = tmxr_poll_conn (&dc_desc);                     /* look for connect */
    if (ln >= 0) {                                      /* got one? rcv enb*/
        dc_ldsc[ln].rcve = 1;
        dc_ring |= (1 << ln);
        dc_l_status |= (1LL << (ln + dc_modem));        /* Flag modem line */
        sim_debug(DEBUG_DETAIL, &dc_dev, "DC line connect %d\n", ln);
    }
    tmxr_poll_tx(&dc_desc);
    tmxr_poll_rx(&dc_desc);
    for (ln = 0; ln < dc_desc.lines; ln++) {
       /* Check if buffer empty */
       if (dc_ldsc[ln].xmte && ((dc_l_status & (1ll << ln)) != 0)) {
           tx_enable |= 1 << ln;
       }

       /* Check to see if any pending data for this line */
       if (tmxr_rqln(&dc_ldsc[ln]) > 0) {
           rx_rdy |= (1 << ln);
           dc_l_status |= (1LL << ln);                  /* Flag line */
           sim_debug(DEBUG_DETAIL, &dc_dev, "DC recieve %d\n", ln);
       }
       /* Check if disconnect */
       if ((rx_conn & (1 << ln)) != 0 && dc_ldsc[ln].conn == 0) {
           rx_conn &= ~(1 << ln);
           dc_l_status |= (1LL << (ln + dc_modem));     /* Flag modem line */
           sim_debug(DEBUG_DETAIL, &dc_dev, "DC line disconnect %d\n", ln);
       }
    }

    /* If any pending status request, raise the PI signal */
    if (dc_l_status)
        set_interrupt(DC_DEVNUM, uptr->STATUS);
    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */
    return SCPE_OK;
}

/* Scan to see if something to do */
t_stat dc_doscan (UNIT *uptr) {
   int32 lmask;

   uptr->STATUS &= ~(RCV_PI|XMT_PI);
   clr_interrupt(DC_DEVNUM);
   for (;dc_l_status != 0; dc_l_count++) {
      dc_l_count &= 077;
      /* Check if we found it */
      if (dc_l_status & (1LL << dc_l_count)) {
         /* Check if modem control or data line */
         if (dc_l_count >= dc_modem) {
            uptr->STATUS |= RCV_PI;
         } else {
            /* Must be data line */
            lmask = 1 << dc_l_count;
            if (rx_rdy & lmask)
                uptr->STATUS |= RCV_PI;
            if (tx_enable & lmask)
                uptr->STATUS |= XMT_PI;
         }
         /* Stop scanner */
         set_interrupt(DC_DEVNUM, uptr->STATUS);
         return SCPE_OK;
      }
   }
   return SCPE_OK;
}

/* Reset routine */

t_stat dc_reset (DEVICE *dptr)
{

    if (dc_unit.flags & UNIT_ATT)                           /* if attached, */
        sim_activate (&dc_unit, tmxr_poll);                 /* activate */
    else
        sim_cancel (&dc_unit);                             /* else stop */
    tx_enable = 0;
    rx_rdy = 0;                             /* Flags */
    rx_conn = 0;
    dc_l_status = 0;
    dc_l_count = 0;
    dc_unit.STATUS = 0;
    clr_interrupt(DC_DEVNUM);
    return SCPE_OK;
}


/* SET BUFFER processor */

t_stat dc_set_modem (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 modem;

    if (cptr == NULL)
        return SCPE_ARG;
    modem = (int32) get_uint (cptr, 10, 32, &r);
    if (r != SCPE_OK)
        return SCPE_ARG;
    if (modem < 0 || modem >= (DC10_MLINES * 2))
        return SCPE_ARG;
    if (modem < dc_desc.lines)
        return SCPE_ARG;
    if ((modem % 8) == 0) {
        dc_modem = modem;
        return SCPE_OK;
    }
    return SCPE_ARG;
}

/* SHOW BUFFER processor */

t_stat dc_show_modem (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (st, "modem=%d ", dc_modem);
    return SCPE_OK;
}

/* SET LINES processor */

t_stat dc_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 newln, i, t;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    newln = (int32) get_uint (cptr, 10, DC10_MLINES, &r);
    if ((r != SCPE_OK) || (newln == dc_desc.lines))
        return r;
    if (newln > dc_modem)
        return SCPE_ARG;
    if ((newln == 0) || (newln >= DC10_MLINES) || (newln % 8) != 0)
        return SCPE_ARG;
    if (newln < dc_desc.lines) {
        for (i = newln, t = 0; i < dc_desc.lines; i++)
            t = t | dc_ldsc[i].conn;
        if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
            return SCPE_OK;
        for (i = newln; i < dc_desc.lines; i++) {
            if (dc_ldsc[i].conn) {
                tmxr_linemsg (&dc_ldsc[i], "\r\nOperator disconnected line\r\n");
                tmxr_send_buffered_data (&dc_ldsc[i]);
                }
            tmxr_detach_ln (&dc_ldsc[i]);               /* completely reset line */
        }
    }
    if (dc_desc.lines < newln)
        memset (dc_ldsc + dc_desc.lines, 0, sizeof(*dc_ldsc)*(newln-dc_desc.lines));
    dc_desc.lines = newln;
    return dc_reset (&dc_dev);                         /* setup lines and auto config */
}

/* SET LOG processor */

t_stat dc_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    char gbuf[CBUFSIZE];
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    cptr = get_glyph (cptr, gbuf, '=');
    if ((cptr == NULL) || (*cptr == 0) || (gbuf[0] == 0))
        return SCPE_ARG;
    ln = (int32) get_uint (gbuf, 10, dc_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= dc_desc.lines))
        return SCPE_ARG;
    return tmxr_set_log (NULL, ln, cptr, desc);
}

/* SET NOLOG processor */

t_stat dc_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    ln = (int32) get_uint (cptr, 10, dc_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= dc_desc.lines))
        return SCPE_ARG;
    return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

t_stat dc_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32 i;

    for (i = 0; i < dc_desc.lines; i++) {
        fprintf (st, "line %d: ", i);
        tmxr_show_log (st, NULL, i, desc);
        fprintf (st, "\n");
        }
    return SCPE_OK;
}


/* Attach routine */

t_stat dc_attach (UNIT *uptr, CONST char *cptr)
{
t_stat reason;

reason = tmxr_attach (&dc_desc, uptr, cptr);
if (reason != SCPE_OK)
  return reason;
sim_activate (uptr, tmxr_poll);
return SCPE_OK;
}

/* Detach routine */

t_stat dc_detach (UNIT *uptr)
{
  int32  i;
  t_stat reason;
reason = tmxr_detach (&dc_desc, uptr);
for (i = 0; i < dc_desc.lines; i++)
    dc_ldsc[i].rcve = 0;
sim_cancel (uptr);
return reason;
}

t_stat dc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "DC10E Terminal Interfaces\n\n");
fprintf (st, "The DC10 supported up to 8 blocks of 8 lines. Modem control was on a seperate\n");
fprintf (st, "line. The simulator supports this by setting modem control to a fixed offset\n");
fprintf (st, "from the given line. The number of lines is specified with a SET command:\n\n");
fprintf (st, "   sim> SET DC LINES=n          set number of additional lines to n [8-32]\n\n");
fprintf (st, "Lines must be set in multiples of 8.\n");
fprintf (st, "The default offset for modem lines is 32. This can be changed with\n\n");
fprintf (st, "   sim> SET DC MODEM=n          set offset for modem control to n [8-32]\n\n");
fprintf (st, "Modem control must be set larger then the number of lines\n");
fprintf (st, "The ATTACH command specifies the port to be used:\n\n");
tmxr_attach_help (st, dptr, uptr, flag, cptr);
fprintf (st, "The additional terminals can be set to one of four modes: UC, 7P, 7B, or 8B.\n\n");
fprintf (st, "  mode  input characters        output characters\n\n");
fprintf (st, "  UC    lower case converted    lower case converted to upper case,\n");
fprintf (st, "        to upper case,          high-order bit cleared,\n");
fprintf (st, "        high-order bit cleared  non-printing characters suppressed\n");
fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                                non-printing characters suppressed\n");
fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
fprintf (st, "  8B    no changes              no changes\n\n");
fprintf (st, "The default mode is 7P.\n");
fprintf (st, "Finally, each line supports output logging.  The SET DCn LOG command enables\n");
fprintf (st, "logging on a line:\n\n");
fprintf (st, "   sim> SET DCn LOG=filename   log output of line n to filename\n\n");
fprintf (st, "The SET DCn NOLOG command disables logging and closes the open log file,\n");
fprintf (st, "if any.\n\n");
fprintf (st, "Once DC is attached and the simulator is running, the terminals listen for\n");
fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
fprintf (st, "by the Telnet client, a SET DC DISCONNECT command, or a DETACH DC command.\n\n");
fprintf (st, "Other special commands:\n\n");
fprintf (st, "   sim> SHOW DC CONNECTIONS    show current connections\n");
fprintf (st, "   sim> SHOW DC STATISTICS     show statistics for active connections\n");
fprintf (st, "   sim> SET DCn DISCONNECT     disconnects the specified line.\n");
fprint_reg_help (st, &dc_dev);
fprintf (st, "\nThe additional terminals do not support save and restore.  All open connections\n");
fprintf (st, "are lost when the simulator shuts down or DC is detached.\n");
return SCPE_OK;
}

const char *dc_description (DEVICE *dptr)
{
return "DC10E asynchronous line interface";
}

#endif
