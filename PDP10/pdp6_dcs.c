/* pdp6_dcs.c: PDP-6 DC630 communication server simulator

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

#ifndef NUM_DEVS_DCS
#define NUM_DEVS_DCS 0
#endif

#if (NUM_DEVS_DCS > 0)

#define DCS_DEVNUM 0300

#define DCS_LINES    16


#define STATUS   u3

#define RPI_CHN  000007         /* IN STATUS. */
#define TPI_CHN  000700         /* In STATUS */
#define RLS_SCN  000010         /* CONO DCSA release scanner */
#define RST_SCN  000020         /* CONO DCSA reset to 0 */
#define RSCN_ACT 000040         /* Scanner line is active */
#define XMT_RLS  004000         /* Clear transmitter flag */
#define XSCN_ACT 004000         /* Transmit scanner active */

#define DATA     0000377
#define LINE     0000077        /* Line number in Left */


int      dcs_rx_scan = 0;                        /* Scan counter */
int      dcs_tx_scan = 0;                        /* Scan counter */
int      dcs_send_line = 0;                      /* Send line number */
TMLN     dcs_ldsc[DCS_LINES] = { 0 };            /* Line descriptors */
TMXR     dcs_desc = { DCS_LINES, 0, 0, dcs_ldsc };
uint32   dcs_tx_enable, dcs_rx_rdy;              /* Flags */
uint32   dcs_enable;                             /* Enable line */
uint32   dcs_rx_conn;                            /* Connection flags */
extern int32 tmxr_poll;

t_stat dcs_devio(uint32 dev, uint64 *data);
t_stat dcs_svc (UNIT *uptr);
t_stat dcs_doscan (UNIT *uptr);
t_stat dcs_reset (DEVICE *dptr);
t_stat dcs_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dcs_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dcs_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dcs_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dcs_attach (UNIT *uptr, CONST char *cptr);
t_stat dcs_detach (UNIT *uptr);
t_stat dcs_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr);
const char *dcs_description (DEVICE *dptr);

/* Type 630 data structures

   dcs_dev      Type 630 device descriptor
   dcs_unit     Type 630 unit descriptor
   dcs_reg      Type 630 register list
*/


#if !PDP6
#define D DEV_DIS
#else
#define D 0
#endif

DIB dcs_dib = { DCS_DEVNUM, 2, &dcs_devio, NULL };

UNIT dcs_unit = {
    UDATA (&dcs_svc, TT_MODE_7B+UNIT_IDLE+UNIT_ATTABLE, 0), KBD_POLL_WAIT
    };

REG dcs_reg[] = {
    { DRDATA (TIME, dcs_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (STATUS, dcs_unit.STATUS, 18), PV_LEFT },
    { NULL }
    };

MTAB dcs_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", NULL },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  NULL },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  NULL },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &dcs_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &dcs_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dcs_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dcs_desc, "Display multiplexer statistics" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dcs_setnl, &tmxr_show_lines, (void *) &dcs_desc, "Set number of lines" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "LOG=n=file",
        &dcs_set_log, NULL, (void *)&dcs_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "NOLOG",
        &dcs_set_nolog, NULL, (void *)&dcs_desc, "Disable logging on designated line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "LOG", NULL,
        NULL, &dcs_show_log, (void *)&dcs_desc, "Display logging for all lines" },
    { 0 }
    };

DEVICE dcs_dev = {
    "DCS", &dcs_unit, dcs_reg, dcs_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &dcs_reset,
    NULL, &dcs_attach, &dcs_detach,
    &dcs_dib, DEV_MUX | DEV_DISABLE | DEV_DEBUG | D, 0, dev_debug,
    NULL, NULL, &dcs_help, NULL, NULL, &dcs_description
    };


/* IOT routine */
t_stat dcs_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &dcs_unit;
    TMLN *lp;
    int   ln;

    switch(dev & 7) {
    case CONI:
         /* Check if we might have any interrupts pending */
         if ((uptr->STATUS & (RSCN_ACT|XSCN_ACT)) != 0)
             dcs_doscan(uptr);
         *data = uptr->STATUS & (RPI_CHN|TPI_CHN);
         if ((uptr->STATUS & (RSCN_ACT)) == 0)
            *data |= 010LL;
         if ((uptr->STATUS & (XSCN_ACT)) == 0)
            *data |= 01000LL;
         sim_debug(DEBUG_CONI, &dcs_dev, "DCS %03o CONI %06o PC=%o\n",
               dev, (uint32)*data, PC);
         break;

    case CONO:
         /* Set PI */
         uptr->STATUS &= ~(RPI_CHN|TPI_CHN);
         uptr->STATUS |= (RPI_CHN|TPI_CHN) & *data;
         if (*data & RST_SCN)
             dcs_rx_scan = 0;
         if ((*data & (RLS_SCN|RST_SCN)) != 0)
             uptr->STATUS |= RSCN_ACT;
         if ((*data & (XMT_RLS)) != 0) {
             uptr->STATUS |= XSCN_ACT;
             dcs_tx_enable &= ~(1 << dcs_tx_scan);
         }

         sim_debug(DEBUG_CONO, &dcs_dev, "DCS %03o CONO %06o PC=%06o\n",
               dev, (uint32)*data, PC);
         dcs_doscan(uptr);
         break;

    case DATAO:
    case DATAO|4:
         ln = (dev & 4) ? dcs_send_line : dcs_tx_scan;
         if (ln < dcs_desc.lines) {
             lp = &dcs_ldsc[ln];
             if (lp->conn) {
                int32 ch = *data & DATA;
                ch = sim_tt_outcvt(ch, TT_GET_MODE (dcs_unit.flags) | TTUF_KSR);
                tmxr_putc_ln (lp, ch);
                dcs_tx_enable |= (1 << ln);
             }
         }
         if (dev & 4) {
             uptr->STATUS |= XSCN_ACT;
             dcs_doscan(uptr);
         }
         sim_debug(DEBUG_DATAIO, &dcs_dev, "DC %03o DATO %012llo PC=%06o\n",
                    dev, *data, PC);
         break;

    case DATAI:
    case DATAI|4:
         ln = dcs_rx_scan;
         if (ln < dcs_desc.lines) {
             /* Nothing happens if no recieve data, which is transmit ready */
             lp = &dcs_ldsc[ln];
             if (tmxr_rqln (lp) > 0) {
                int32 ch = tmxr_getc_ln (lp);
                if (ch & SCPE_BREAK)                      /* break? */
                    ch = 0;
                else
                    ch = sim_tt_inpcvt (ch, TT_GET_MODE(dcs_unit.flags) | TTUF_KSR);
                *data = (uint64)(ch & DATA);
                dcs_tx_enable &= ~(1 << ln);
             }
             dcs_rx_rdy &= ~(1 << ln);
         }
         if (dev & 4) {
             uptr->STATUS |= RSCN_ACT;
             dcs_doscan(uptr);
         }
         sim_debug(DEBUG_DATAIO, &dcs_dev, "DCS %03o DATI %012llo PC=%06o\n",
                    dev, *data, PC);
         break;
    case CONI|4:
         /* Read in scanner */
         if ((uptr->STATUS & (RSCN_ACT)) != 0)
             *data = (uint64)(dcs_tx_scan) + 2;
         else
             *data = (uint64)(dcs_rx_scan) + 2;
         sim_debug(DEBUG_CONI, &dcs_dev, "DCS %03o CONI %06o PC=%o recieve line\n",
               dev, (uint32)*data, PC);
         break;

    case CONO|4:
         /* Output buffer pointer */
         dcs_send_line = (int)(*data & 077) - 2;
         sim_debug(DEBUG_CONO, &dcs_dev, "DCS %03o CONO %06o PC=%06o send line\n",
               dev, (uint32)*data, PC);
         break;
    }
    return SCPE_OK;
}


/* Unit service */

t_stat dcs_svc (UNIT *uptr)
{
int32 ln;

    if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
        return SCPE_OK;
    ln = tmxr_poll_conn (&dcs_desc);                     /* look for connect */
    if (ln >= 0) {                                      /* got one? rcv enb*/
        dcs_ldsc[ln].rcve = 1;
        dcs_tx_enable |= 1 << ln;
        sim_debug(DEBUG_DETAIL, &dcs_dev, "DC line connect %d\n", ln);
    }
    tmxr_poll_tx(&dcs_desc);
    tmxr_poll_rx(&dcs_desc);
    for (ln = 0; ln < dcs_desc.lines; ln++) {
       /* Check to see if any pending data for this line */
       if (tmxr_rqln(&dcs_ldsc[ln]) > 0) {
           dcs_rx_rdy |= (1 << ln);
           sim_debug(DEBUG_DETAIL, &dcs_dev, "DC recieve %d\n", ln);
       }
       /* Check if disconnect */
       if ((dcs_rx_conn & (1 << ln)) != 0 && dcs_ldsc[ln].conn == 0) {
           dcs_tx_enable &= ~(1 << ln);
           dcs_rx_conn &= ~(1 << ln);
           sim_debug(DEBUG_DETAIL, &dcs_dev, "DC line disconnect %d\n", ln);
       }
    }

    /* If any pending status request, raise the PI signal */
    dcs_doscan(uptr);
    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */
    return SCPE_OK;
}

/* Scan to see if something to do */
t_stat dcs_doscan (UNIT *uptr) {

   clr_interrupt(DCS_DEVNUM);
   if ((uptr->STATUS & (RSCN_ACT)) != 0) {
       for (;dcs_rx_rdy != 0; dcs_rx_scan++) {
          dcs_rx_scan &= 037;
          /* Check if we found it */
          if (dcs_rx_rdy & (1 << dcs_rx_scan)) {
             uptr->STATUS &= ~RSCN_ACT;
             /* Stop scanner */
             set_interrupt(DCS_DEVNUM, uptr->STATUS);
             return SCPE_OK;
          }
       }
   }
   if ((uptr->STATUS & (XSCN_ACT)) != 0) {
       for (;dcs_tx_enable != 0; dcs_tx_scan++) {
          dcs_tx_scan &= 037;
          /* Check if we found it */
          if (dcs_tx_enable & (1 << dcs_tx_scan)) {
             uptr->STATUS &= ~XSCN_ACT;
             /* Stop scanner */
             set_interrupt(DCS_DEVNUM, (uptr->STATUS >> 6));
             return SCPE_OK;
          }
       }
   }
   return SCPE_OK;
}

/* Reset routine */

t_stat dcs_reset (DEVICE *dptr)
{
    if (dcs_unit.flags & UNIT_ATT)                           /* if attached, */
        sim_activate (&dcs_unit, tmxr_poll);                 /* activate */
    else
        sim_cancel (&dcs_unit);                             /* else stop */
    dcs_tx_enable = 0;
    dcs_rx_rdy = 0;                             /* Flags */
    dcs_rx_conn = 0;
    dcs_send_line = 0;
    dcs_tx_scan = 0;
    dcs_rx_scan = 0;
    dcs_unit.STATUS = 0;
    clr_interrupt(DCS_DEVNUM);
    return SCPE_OK;
}


/* SET LINES processor */

t_stat dcs_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 newln, i, t;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    newln = (int32) get_uint (cptr, 10, DCS_LINES, &r);
    if ((r != SCPE_OK) || (newln == dcs_desc.lines))
        return r;
    if ((newln == 0) || (newln >= DCS_LINES) || (newln % 8) != 0)
        return SCPE_ARG;
    if (newln < dcs_desc.lines) {
        for (i = newln, t = 0; i < dcs_desc.lines; i++)
            t = t | dcs_ldsc[i].conn;
        if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
            return SCPE_OK;
        for (i = newln; i < dcs_desc.lines; i++) {
            if (dcs_ldsc[i].conn) {
                tmxr_linemsg (&dcs_ldsc[i], "\r\nOperator disconnected line\r\n");
                tmxr_send_buffered_data (&dcs_ldsc[i]);
                }
            tmxr_detach_ln (&dcs_ldsc[i]);               /* completely reset line */
        }
    }
    if (dcs_desc.lines < newln)
        memset (dcs_ldsc + dcs_desc.lines, 0, sizeof(*dcs_ldsc)*(newln-dcs_desc.lines));
    dcs_desc.lines = newln;
    return dcs_reset (&dcs_dev);                         /* setup lines and auto config */
}

/* SET LOG processor */

t_stat dcs_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    char gbuf[CBUFSIZE];
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    cptr = get_glyph (cptr, gbuf, '=');
    if ((cptr == NULL) || (*cptr == 0) || (gbuf[0] == 0))
        return SCPE_ARG;
    ln = (int32) get_uint (gbuf, 10, dcs_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= dcs_desc.lines))
        return SCPE_ARG;
    return tmxr_set_log (NULL, ln, cptr, desc);
}

/* SET NOLOG processor */

t_stat dcs_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    ln = (int32) get_uint (cptr, 10, dcs_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= dcs_desc.lines))
        return SCPE_ARG;
    return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

t_stat dcs_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32 i;

    for (i = 0; i < dcs_desc.lines; i++) {
        fprintf (st, "line %d: ", i);
        tmxr_show_log (st, NULL, i, desc);
        fprintf (st, "\n");
        }
    return SCPE_OK;
}


/* Attach routine */

t_stat dcs_attach (UNIT *uptr, CONST char *cptr)
{
t_stat reason;

reason = tmxr_attach (&dcs_desc, uptr, cptr);
if (reason != SCPE_OK)
  return reason;
sim_activate (uptr, tmxr_poll);
return SCPE_OK;
}

/* Detach routine */

t_stat dcs_detach (UNIT *uptr)
{
  int32  i;
  t_stat reason;
reason = tmxr_detach (&dcs_desc, uptr);
for (i = 0; i < dcs_desc.lines; i++)
    dcs_ldsc[i].rcve = 0;
sim_cancel (uptr);
return reason;
}

t_stat dcs_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Type 630 Terminal Interfaces\n\n");
fprintf (st, "The Type 630 supported up to 8 blocks of 8 lines. Modem control was on a seperate\n");
fprintf (st, "line. The number of lines is specified with a SET command:\n\n");
fprintf (st, "   sim> SET DCS LINES=n          set number of additional lines to n [8-32]\n\n");
fprintf (st, "Lines must be set in multiples of 8.\n");
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
fprintf (st, "   sim> SET DCSn LOG=filename   log output of line n to filename\n\n");
fprintf (st, "The SET DCSn NOLOG command disables logging and closes the open log file,\n");
fprintf (st, "if any.\n\n");
fprintf (st, "Once DC is attached and the simulator is running, the terminals listen for\n");
fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
fprintf (st, "by the Telnet client, a SET DC DISCONNECT command, or a DETACH DC command.\n\n");
fprintf (st, "Other special commands:\n\n");
fprintf (st, "   sim> SHOW DCS CONNECTIONS    show current connections\n");
fprintf (st, "   sim> SHOW DCS STATISTICS     show statistics for active connections\n");
fprintf (st, "   sim> SET DCSn DISCONNECT     disconnects the specified line.\n");
fprint_reg_help (st, &dcs_dev);
fprintf (st, "\nThe additional terminals do not support save and restore.  All open connections\n");
fprintf (st, "are lost when the simulator shuts down or DC is detached.\n");
return SCPE_OK;
}

const char *dcs_description (DEVICE *dptr)
{
return "Type 630 asynchronous line interface";
}

#endif
