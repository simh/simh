/* pdp18b_g2.c: PDP-7/9 Bell Labs "GRAPHIC-2" subsystem (as a TTY!!)
   from 13-Sep-15 version of pdp18b_tt1.c

   Copyright (c) 1993-2015, Robert M Supnik
   Copyright (c) 2016, Philip L Budne

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   Doug McIlroy had this to say about the BTL PDP-7 Ken Thompson
   created UNIX on:

      The pdp7 was cast off by the visual and acoustics research department.
      Bill Ninke et al. built graphic II on it -- a graphics attachment as big
      as the pdp7 itself. The disk was an amazing thing about 2' in diameter,
      mounted on a horizontal axis. Mystery crashes bedeviled it until somebody
      realized that the axis was perpendicular to the loading dock 4 floors
      below. A 90-degree turn solved the problem.

   The graphics system consists of eleven PDP-7 "devices",
   UNIX only uses six, and only three of the six are simulated here
   (and *JUST* enough of those to figure out the text being displayed)!!

   G2D1         GRAPHICS-2 display output
   G2DS         GRAPHICS-2 display status
   G2KB         GRAPHICS-2 keyboard
   G2PB         GRAPHICS-2 push buttons

   17-Mar-16    PLB     Cloned from 13-Sep-15 version of pdp18b_tt1.c
*/

#include "pdp18b_defs.h"
#ifdef GRAPHICS2
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

uint32 g2do_buf = 0;                    /* output char */

uint8 g2kb_done = 0;                    /* keyboard flag */
uint32 g2kb_buf = 0;                    /* keyboard buffer */

uint8 g2pb_done = 0;                    /* button flag */
uint32 g2pb_bbuf = 0;                   /* button buffer */
uint32 g2pb_lbuf = 0;                   /* button lights */

uint32 g2_dpyaddr = 0;                  /* display address */
int32 g2_dpycount = 0;                  /* character count */

/* terminal mux data */
TMLN g2_ldsc = { 0 };                   /* line descriptor */
TMXR g2_desc = { 1, 0, 0, &g2_ldsc };   /* mux descriptor */

/* kernel display lists always start like this: */
static const int32 g2_expect[3] = {
    0065057, /* PARAM: clear blink, clear light pen, scale=1, intensity=3 */
    0147740, /* X-Y: invisible, no delay, Y=01740 (992) */
    0160000  /* X-Y: invisible, settling delay, X=0 */
};

extern int32 *M;
extern int32 int_hwre[API_HLVL+1];
extern int32 api_vec[API_HLVL][32];
extern int32 tmxr_poll;
extern int32 stop_inst;

int32 g2d1 (int32 dev, int32 pulse, int32 dat);
int32 g2kb (int32 dev, int32 pulse, int32 dat);
int32 g2pb (int32 dev, int32 pulse, int32 dat);

t_stat g2kb_svc (UNIT *uptr);
t_bool g2kb_test_done ();
void g2kb_set_done ();
void g2kb_clr_done ();

t_bool g2pb_test_done ();
void g2pb_set_done ();
void g2pb_clr_done ();

t_stat g2d1_svc (UNIT *uptr);

t_stat g2_attach (UNIT *uptr, char *cptr);
t_stat g2_detach (UNIT *uptr);
t_stat g2_reset (DEVICE *dptr);

/* G2 keyboard data structures

   g2kb_dev     G2kb device descriptor
   g2kb_unit    G2kb unit descriptor
   g2kb_reg     G2kb register list
   g2kb_mod     G2kb modifiers list
*/

/* push button device number is contiguous with keyboard */
DIB g2kb_dib = { DEV_G2KB, 2, NULL, { &g2kb, &g2pb } };

UNIT g2kb_unit = {
    UDATA (&g2kb_svc, UNIT_IDLE|UNIT_ATTABLE, 0), KBD_POLL_WAIT
};

REG g2kb_reg[] = {
    { ORDATA (BUF, g2kb_buf, 1) },
    { ORDATA (DONE, g2kb_done, 1) },
    { FLDATA (INT, int_hwre[API_G2], INT_V_G2) },
    { DRDATA (TIME, g2kb_unit.wait, 24), REG_NZ + PV_LEFT },
    { ORDATA (BUTTONS, g2pb_bbuf, 1) },
    { ORDATA (LITES, g2pb_lbuf, 1) },
    { NULL }
    };

MTAB g2kb_mod[] = {
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &g2_desc },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, (void *) &g2_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &g2_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &g2_desc },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      NULL, &show_devno, NULL },
    { 0 }
    };

DEVICE g2kb_dev = {
    "G2KB",                             /* name */
    &g2kb_unit,                         /* units */
    g2kb_reg, g2kb_mod,                 /* registers, modifiers */
    1,                                  /* numunits */
    10, 31,                             /* aradix, awidth */
    1, 8, 8,                            /* aincr, dradix, dwidth */
    &tmxr_ex, &tmxr_dep, &g2_reset,     /* examine, deposit, reset */
    NULL, &g2_attach, &g2_detach,       /* boot, attach, detach */
    &g2kb_dib, DEV_MUX | DEV_DISABLE    /* ctxt, flags */
    };

/* G2 Display Output Device 1 data structures
   g2d1_dev     g2d1 device descriptor
   g2d1_unit    g2d1 unit descriptor
   g2d1_reg     g2d1 register list
*/

DIB g2d1_dib = { DEV_G2D1, 1, NULL, { &g2d1 } };

UNIT g2d1_unit = { UDATA (&g2d1_svc, 0, 0), SERIAL_OUT_WAIT };

REG g2d1_reg[] = {
    { ORDATA (DPYADDR, g2_dpyaddr, 1) },
    { FLDATA (INT, int_hwre[API_G2], INT_V_G2) },
    { URDATA (TIME, g2d1_unit.wait, 10, 24, 0, 1, PV_LEFT) },
    { NULL }
    };

MTAB g2d1_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &g2_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &g2_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &g2_desc },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      NULL, &show_devno, NULL },
    { 0 }
    };

DEVICE g2d1_dev = {
    "G2D1",                             /* name */
    &g2d1_unit,                         /* units */
    g2d1_reg, g2d1_mod,                 /* registers, modifiers */
    1,                                  /* numunits */
    10, 31,                             /* aradix, awidth */
    1, 8, 8,                            /* aincr, dradix, dwidth */
    NULL, NULL, &g2_reset,              /* examine, deposit, reset */
    NULL, NULL, NULL,                   /* boot, attach, detach */
    &g2d1_dib, DEV_DISABLE              /* ctxt, flags */
    };

/****************************************************************
 * IOT routines
 */

/* Keyboard input IOT routine */
/* real device might have done bitwise decode?! */
int32 g2kb (int32 dev, int32 pulse, int32 dat)
{
if (pulse == 001) {                     /* sck */
    if (g2kb_done) {
        dat = dat | IOT_SKP;
        }
    }
else if (pulse == 002) {                /* lck */
    g2kb_clr_done ();                   /* clear flag */
    dat = dat | g2kb_buf;               /* return buffer */
    }
else if (pulse == 004) {                /* cck */
    g2kb_clr_done ();                   /* clear flag */
    }
return dat;
}

/* Push Button: IOT routine */
int32 g2pb (int32 dev, int32 pulse, int32 dat)
{
if (pulse & 020) {                                      /* wbl */
    /* XXX if light for pb 7, press button 7!! */
    printf("G2: wbl %#o\r\n", dat);
    g2pb_lbuf = dat;
    }
if (pulse & 001) {                                      /* spb */
    if (g2pb_done) {
        dat = dat | IOT_SKP;
    }
}
if (pulse & 002) {                                      /* lpb */
    g2pb_clr_done ();                                   /* clear flag */
    dat = dat | g2pb_bbuf;                              /* return buttons */
    }
if (pulse & 004) {                                      /* cpb */
    g2pb_clr_done ();                                   /* clear flag */
    }
}

/* Unit service */

t_stat g2kb_svc (UNIT *uptr)
{
int32 ln, c, temp;

if ((uptr->flags & UNIT_ATT) == 0)              /* attached? */
    return SCPE_OK;
sim_clock_coschedule (uptr, tmxr_poll);         /* continue poll */
ln = tmxr_poll_conn (&g2_desc);                 /* look for connect */
if (ln >= 0)                                    /* got one? rcv enab */
    g2_ldsc.rcve = 1;
tmxr_poll_rx (&g2_desc);                        /* poll for input */
if (g2_ldsc.conn) {                             /* connected? */
    if ((temp = tmxr_getc_ln (&g2_ldsc))) {     /* get char */
        if (temp & SCPE_BREAK)                  /* break? */
            c = 0;
        else c = sim_tt_inpcvt (temp, TT_GET_MODE(g2d1_unit.flags) );
        g2kb_buf = c;
        g2kb_set_done ();
        }
    }
return SCPE_OK;
}

/****************************************************************/
/* Interrupt handling routines */

t_bool g2kb_test_done ()
{
if (g2kb_done)
    return TRUE;
return FALSE;
}

void g2kb_set_done ()
{
g2kb_done = 1;
SET_INT (G2);
return;
}

void g2kb_clr_done ()
{
g2kb_done = 0;
CLR_INT (G2);
return;
}

/****************/

t_bool g2pb_test_done ()
{
if (g2pb_done)
    return TRUE;
return FALSE;
}

void g2pb_set_done ()
{
g2pb_done = 1;
SET_INT (G2);
return;
}

void g2pb_clr_done ()
{
g2pb_done = 0;
CLR_INT (G2);
return;
}

/****************************************************************/
/* Display Output: IOT routine */

/*
 * UNIX text display command lists always end with a TRAP
 * and display output is restarted periodicly in timer PI service code
 */

static void g2_putchar(char c)
{
if (g2_ldsc.conn && g2_ldsc.xmte) { /* connected, tx enabled? */
    tmxr_putc_ln (&g2_ldsc, c);
    if (c == '\n')
        tmxr_putc_ln (&g2_ldsc, '\r');
    g2_dpycount++;            /* only consume if connected+enabled! */
}
}

int32 g2d1 (int32 dev, int32 pulse, int32 dat)
{
if (g2_ldsc.conn && g2_ldsc.xmte && pulse == 047) { /* beg */
    int32 n = g2_dpycount, i;
    g2_dpyaddr = dat & 017777;
    for (i = g2_dpyaddr; i < 020000; i++) {
        uint32 w = M[i] & 0777777;
        if (w & 0400000)                /* TRAP? */
            break;
        /* check first three words for expected setup commands */
        int o = i - g2_dpyaddr;
        if (o < sizeof(g2_expect)/sizeof(g2_expect[0])) {
            if (w != g2_expect[o]) {
                printf("g2: unexpected command at %#o: %#o expected %#o\r\n",
                       i, w, g2_expect[o]);
                break;
            }
            continue;
        }
        if (w & 0300000)        { /* not characters? */
            printf("g2: unexpected command at %#o: %#o\r\n", i, w);
            break;
        }
        if (--n < 0)                    /* new? */
            g2_putchar( (w>>7) & 0177 );

        if ((w & 0177) && --n < 0)      /* char2 & new? */
            g2_putchar( w & 0177 );

    } /* for loop */
    fflush(stdout);             /* TEMP */
    if (n > 0)
        g2_dpycount = 0;        /* didn't see as much as last time? */
} /* beg IOT */
return dat;
}

/* Unit service */

t_stat g2d1_svc (UNIT *uptr)
{
if (g2_ldsc.conn) {                     /* connected? */
    tmxr_poll_tx (&g2_desc);            /* poll xmt */
    if (!g2_ldsc.xmte) {                /* tx not enabled? */
        sim_activate (uptr, g2d1_unit.wait); /* wait */
    }
}
return SCPE_OK;
}


/* Reset routine */
t_stat g2_reset (DEVICE *dptr)
{
if (dptr->flags & DEV_DIS) {                            /* sync enables */
    g2kb_dev.flags = g2kb_dev.flags | DEV_DIS;
    g2d1_dev.flags = g2d1_dev.flags | DEV_DIS;
    }
else {
    g2kb_dev.flags = g2kb_dev.flags & ~DEV_DIS;
    g2d1_dev.flags = g2d1_dev.flags & ~DEV_DIS;
    }
if (g2kb_unit.flags & UNIT_ATT)                         /* if attached, */
    sim_activate (&g2kb_unit, tmxr_poll);               /* activate */
else sim_cancel (&g2kb_unit);                           /* else stop */

g2kb_buf = 0;                                           /* clear buf */
g2pb_bbuf = 0;                                          /* clear buttons */
g2pb_lbuf = 0;                                          /* clear lites */
g2_dpyaddr = 0;
g2_dpycount = 0;
g2kb_clr_done ();                                       /* clear done */
g2pb_clr_done ();
sim_cancel (&g2d1_unit);                                /* stop poll */
return SCPE_OK;
}

/* Attach master unit */
t_stat g2_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = tmxr_attach (&g2_desc, uptr, cptr); /* attach */
if (r != SCPE_OK)                       /* error */
    return r;
sim_activate (uptr, 0);                 /* start poll at once */
return SCPE_OK;
}

/* Detach master unit */

t_stat g2_detach (UNIT *uptr)
{
t_stat r = tmxr_detach (&g2_desc, uptr); /* detach */
sim_cancel (uptr);                       /* stop poll */
g2_ldsc.rcve = 0;
return r;
}
#endif
