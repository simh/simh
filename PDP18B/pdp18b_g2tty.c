/* pdp18b_g2tty.c: PDP-7/9 Bell Labs "GRAPHIC-2" subsystem as a TTY via TELNET
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

   Doug McIlroy had this to say about the Bell Labs PDP-7 Ken Thompson
   created UNIX on:

      The pdp7 was cast off by the visual and acoustics research department.
      Bill Ninke et al. built graphic II on it -- a graphics attachment as big
      as the pdp7 itself. The disk was an amazing thing about 2' in diameter,
      mounted on a horizontal axis. Mystery crashes bedeviled it until somebody
      realized that the axis was perpendicular to the loading dock 4 floors
      below. A 90-degree turn solved the problem.

   GRAPHICS-2 was a command list based graphics display system,
   and included a light pen, a "button box" and status bits
   for a "dataphone" interface to speak to a GECOS system.

   The UNIX-7 system driver only uses text display, and reserves 269
   words (holding two characters each; the buffer is 273 words, but
   three contain display "setup" commands, and the final word in the
   buffer must be a display "TRAP" instruction that ends the display
   list).

   The UNIX system code triggers a refresh every 10 60Hz "ticks" of
   the real time clock.  This driver attempts to do detect new text
   and send it to a user who has TELNETed in.

   Thoughts on implementing a web interface:

   538 characters redisplaying at 6Hz (every 10 "ticks") gives a
   bandwith requirement of only 26Kbit, and most refreshes won't
   change the screen and could be suppressed.  So it seems like it
   would be reasonable to create a web interface.

   Make a SIMH TCP server port which implements a tiny HTTP server.
   The base URL serves up a skeletal page with (lighted) buttons,
   and a "display window".

   And either:

   1) Use "AJAX": an (invisible) <iframe> on in the HTML served by the
      "base URL" keeps a never-ending connection that is sent "script"
      tags to alter the "screen" (div) contents, and light push buttons.
      Keypresses and buttons could be implemented using "onclick"
      actions which trigger a GET (or a POST) on a URLs which
      open new (temporary) HTTP connections to SIMH.  The key/button
      press URL could contain a session UUID which has to match
      a value sent in the initial page.

   2) Have the "home" page HTML establish a bi-directional WebSocket
      connection to Encapsulate all the traffic (screen contents,
      button lighting, key & button presses).

   The graphics system responds as ten PDP-7 "devices";
   UNIX only uses six, and only three of the six are simulated here
   (and *JUST* enough of those to figure out the text being displayed),
   as two SIMH DEVICES, G2OUT and G2IN:

   G2OUT:
       G2D1     005     GRAPHICS-2 display output
   G2IN:
       G2KB     043     GRAPHICS-2 keyboard
       G2BB     044     GRAPHICS-2 button box (lighted bush buttons)

   20-Mar-16    PLB     Works, mostly
   19-Mar-16    PLB     Working (up to a screen full)
   17-Mar-16    PLB     Cloned from 13-Sep-15 version of pdp18b_tt1.c
*/

/*

 * GRAPHICS-2 was vector graphics hardware, UNIX-7 uses it as a
 * "Glass TTY" for a "second seat".
 *
 * This simulation ONLY handles text display; the one program
 * found that uses the "capt" (capture?) call to set a user
 * supplied display list will not work here.
 *
 * When the display buffer or screen is filled, the UNIX "display"
 * driver lights "push button 7" (PB7), and waits for the user to
 * press the button.  UNIX then clears the screen, and output
 * continues.  If the program outputs a "Form Feed" character the
 * display is also cleared.
 *
 * This simulation automatically presses PB7 when lit, without
 * bothering the user (a more accurate simulation might prompt
 * the user to press any key)!
 */

#include "pdp18b_defs.h"
#ifdef GRAPHICS2
#include "sim_tmxr.h"

int debug = 0;

/* hardware registers */
uint8 g2kb_done = 0;                    /* keyboard flag */
uint32 g2kb_buf = 0;                    /* keyboard buffer */
uint8 g2bb_flag = 0;                    /* button flag */
uint32 g2bb_bbuf = 0;                   /* button buffer */
uint32 g2bb_lbuf = 0;                   /* button lights buffer */
uint32 g2out_addr = 0;                  /* display address */
#define PB7 02000

/* not hardware registers: */
uint32 g2out_count = 0;
uint8 g2out_stuffcr = 0;                /* need to stuff a CR */


/* keep old and new version of characters to display
 * a count & checksum of the "old" screen contents might suffice,
 * time will tell....
 */
uint8 g2out_which = 0;
#define OLD g2out_which
#define NEW (g2out_which ^ 1)

#define MAXBUFCHARS 700                 /* larger than kernel display list */
static struct dspbuf {
    uint16 count;
    char buffer[MAXBUFCHARS];           /* 7-bit ASCII */
} g2out_dspbufs[2];

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

/* SIMH G2IN DEVICE */
t_bool g2kb_test_done ();
void g2kb_set_done ();
void g2kb_clr_done ();
int32 g2kb_iot (int32 dev, int32 pulse, int32 dat); /* device 043 */

t_bool g2bb_test_flag ();
void g2bb_set_flag ();
void g2bb_clr_flag ();
int32 g2bb_iot (int32 dev, int32 pulse, int32 dat); /* device 044 */

t_stat g2in_svc (UNIT *uptr);

/* SIMH G2OUT DEVICE */
int32 g2d1_iot (int32 dev, int32 pulse, int32 dat); /* device 05 */
static void g2out_clear ();
static void g2out_process_display_list ();
static int g2out_send_new ();

/* both G2IN/G2OUT: */
t_stat g2_attach (UNIT *uptr, CONST char *cptr);
t_stat g2_detach (UNIT *uptr);
t_stat g2_reset (DEVICE *dptr);

/****************************************************************
 * SIMH G2IN (keyboard/buttons) DEVICE data structures
 *
 *  g2in_dev     G2IN device descriptor
 *  g2in_unit    G2IN unit descriptor
 *  g2in_reg     G2IN register list
 *  g2in_mod     G2IN modifiers list
 */

DIB g2in_dib = { DEV_G2KB, 2, NULL, { &g2kb_iot, &g2bb_iot } };

UNIT g2in_unit = {
    UDATA (&g2in_svc, UNIT_IDLE|UNIT_ATTABLE, 0), KBD_POLL_WAIT
};

REG g2in_reg[] = {
    { ORDATA (KBBUF, g2kb_buf, 1) },
    { ORDATA (KBDONE, g2kb_done, 1) },
    { FLDATA (INT, int_hwre[API_G2], INT_V_G2) },
    { DRDATA (TIME, g2in_unit.wait, 24), REG_NZ + PV_LEFT },
    { ORDATA (BBBBUF, g2bb_bbuf, 1) },  /* button box button buffer */
    { ORDATA (BBFLAG, g2bb_flag, 1) },  /* button box IRQ */
    { ORDATA (BBLBUF, g2bb_lbuf, 1) },  /* button box lights buffer */
    { NULL }
    };

MTAB g2in_mod[] = {
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &g2_desc },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, (void *) &g2_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &g2_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &g2_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &g2_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &g2_desc },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      NULL, &show_devno, NULL },
    { 0 }
    };

/* SIMH G2IN device descriptor (GRAPHICS-2 keyboard & button box) */
DEVICE g2in_dev = {
    "G2IN",                             /* name */
    &g2in_unit,                         /* units */
    g2in_reg, g2in_mod,                 /* registers, modifiers */
    1,                                  /* numunits */
    10, 31,                             /* aradix, awidth */
    1, 8, 8,                            /* aincr, dradix, dwidth */
    &tmxr_ex, &tmxr_dep, &g2_reset,     /* examine, deposit, reset */
    NULL, &g2_attach, &g2_detach,       /* boot, attach, detach */
    &g2in_dib, DEV_MUX | DEV_DISABLE    /* ctxt, flags */
    };

/****************************************************************
 * SIMH G2OUT (display output) DEVICE data structures
 * Only needed to hold "iot" routine, since DIB's can't represent
 * devices with register sets as sparse as GRAPHICS-2
 *
 *  g2out_dev     G2OUT device descriptor
 *  g2out_unit    G2OUT unit descriptor
 *  g2out_reg     G2OUT register list
 *  g2out_mod     G2OUT modifiers list
 */

DIB g2out_dib = { DEV_G2D1, 1, NULL, { &g2d1_iot } };

UNIT g2out_unit = { UDATA (NULL, 0, 0) };

REG g2out_reg[] = {
    { ORDATA (DPYADDR, g2out_addr, 1) },
    { NULL }
    };

MTAB g2out_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &g2_desc },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      NULL, &show_devno, NULL },
    { 0 }
    };

/* SIMH G2OUT device descriptor (simulates just one of many display IOTs!) */
DEVICE g2out_dev = {
    "G2OUT",                            /* name */
    &g2out_unit,                        /* units */
    g2out_reg, g2out_mod,               /* registers, modifiers */
    1,                                  /* numunits */
    10, 31,                             /* aradix, awidth */
    1, 8, 8,                            /* aincr, dradix, dwidth */
    NULL, NULL, &g2_reset,              /* examine, deposit, reset */
    NULL, NULL, NULL,                   /* boot, attach, detach */
    &g2out_dib, DEV_DISABLE             /* ctxt, flags */
    };

/****************************************************************
 * IOT routines
 */

/* Keyboard input IOT routine */
/* real device could have done bitwise decode?! */
int32 g2kb_iot (int32 dev, int32 pulse, int32 dat)
{
if (pulse == 001) {                     /* sck */
    if (g2kb_done) {
        dat = dat | IOT_SKP;
        }
    }
else if (pulse == 002) {                /* lck */
    dat = dat | g2kb_buf;               /* return buffer */
    }
else if (pulse == 004) {                /* cck */
    g2kb_clr_done ();                   /* clear flag */
    }
return dat;
}

/* Button Box IOT routine */
int32 g2bb_iot (int32 dev, int32 pulse, int32 dat)
{
if (pulse == 001) {                     /* "spb" -- skip on push button flag */
    if (g2bb_flag)
        dat = dat | IOT_SKP;
    }
else if (pulse == 002)                  /* "lpb"/"opb" -- or push buttons */
    dat = dat | g2bb_bbuf;              /* return buttons */
else if (pulse == 004) {                /* "cpb" -- clear push button flag */
    g2bb_clr_flag ();                   /* clear flag */
    }
else if (pulse == 024) {                /* "wbl" -- write buttons lights */
    if (dat == 0)
        g2out_clear();                  /* UNIX has ack'ed button press */
    g2bb_lbuf = dat;
    }
return dat;
}

/* Input side Unit service */
t_stat g2in_svc (UNIT *uptr)
{
int32 ln, c;

if ((uptr->flags & UNIT_ATT) == 0)              /* attached? */
    return SCPE_OK;

if (g2bb_lbuf & PB7) {                          /* button 7 lit? */
    /* yes: try sending anything new */
    g2out_process_display_list();
    g2out_send_new();

    g2bb_bbuf |= PB7;                           /* press it to clear screen! */
    g2bb_set_flag ();
    }
sim_clock_coschedule (uptr, tmxr_poll);         /* continue poll */
ln = tmxr_poll_conn (&g2_desc);                 /* look for connect */
if (ln >= 0)                                    /* got one? rcv enab */
    g2_ldsc.rcve = 1;
tmxr_poll_rx (&g2_desc);                        /* poll for input */
if (g2_ldsc.conn) {                             /* connected? */
    tmxr_poll_tx (&g2_desc);                    /* PLB: poll xmt */
    if ((c = tmxr_getc_ln (&g2_ldsc))) {        /* get char */
        if (c & SCPE_BREAK)                     /* break? */
            c = 0;
        else {
            c &= 0177;
            if (c == '\r')                      /* translate CR but not ESC! */
                c = '\n';
            else if ((c & 0155) == 055)         /* kernel swaps around -?/= */
                c ^= 020;                       /* pre-swap!!  */
            }
        g2kb_buf = c;
        g2kb_set_done ();
        }
    } /* connected */
else {
    /* not connected; next connection sees entire "screen" */
    g2out_stuffcr = 0;
    }
return SCPE_OK;
}

/* Interrupt handling routines */

t_bool g2kb_test_done ()
{
return g2kb_done != 0;
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

t_bool g2bb_test_flag ()
{
return g2bb_flag != 0;
}

void g2bb_set_flag ()
{
g2bb_flag = 1;
SET_INT (G2);
return;
}

void g2bb_clr_flag ()
{
g2bb_flag = 0;
CLR_INT (G2);
return;
}

/****************************************************************
 * SIMH G2OUT (Display Output) DEVICE routines
 */

/* helper to put 7-bit display character */
static void g2pc(char c) {
    //if (debug) putchar(c);
    tmxr_putc_ln (&g2_ldsc, c);
}

/* send a character from the display. adds CR after LF */
/* returns 1 if "c" was sent; 0 means try again later */
static int g2out_putchar(char c)
{
if (!g2_ldsc.conn || !g2_ldsc.xmte)     /* connected, tx enabled? */
    return 0;

if (g2out_stuffcr) {                    /* need to stuff a CR? */
    g2pc ('\r');
    g2out_stuffcr = 0;
    if (!g2_ldsc.xmte)                 /* full? */
        return 0;                      /* yes: wait until next time */
    }

g2pc (c);

if (c == '\n') {                        /* was it a NL? */
    if (g2_ldsc.xmte)                   /* transmitter enabled? */
        g2pc ('\r');                    /* send CR now */
    else
        g2out_stuffcr = 1;              /* wait until next time */
    }
return 1;
}

/* Device 05 IOT routine */
int32 g2d1_iot (int32 dev, int32 pulse, int32 dat)
{
/*
 * UNIX text display command lists always end with a TRAP
 * and display output is restarted periodicly in timer PI service code
 */
if (g2_ldsc.conn && g2_ldsc.xmte && pulse == 047) { /* conn&ready, "beg" */
    g2out_addr = dat & 017777;
    g2out_process_display_list();
    g2out_send_new();
    g2out_which ^= 1;                   /* swap buffers */
    } /* beg IOT */
return dat;
}

/****************
 * display buffer management/process
 * we're informed when UNIX wants to clear the screen (PB7 lit)
 * we then press the button
 * UNIX does a "cpb" to ACK/clear the interrupt.
 *
 * *BUT* UNIX clears the screen when a FF (014) char is output,
 * which just resets the buffer (and not issuing any IOTs)
 */

static void g2out_clear() {
    g2out_stuffcr = 0;
    g2out_which = 0;
    g2out_count = 0;
    g2out_dspbufs[0].count = g2out_dspbufs[1].count = 0;
}

/* interpret display list; save characters into "new" dspbuf
 * quits early if display list doesn't conform to what's expected
 */
static void g2out_process_display_list() {
    uint32 i;
    struct dspbuf *dp = g2out_dspbufs + NEW;

    dp->count = 0;
    for (i = g2out_addr; i < 020000; i++) {
        uint32 w = M[i] & 0777777;
        int offset = i - g2out_addr;
        char c;

        if (w & 0400000)                /* TRAP (end of display list) */
            return;

        /* check first three words for expected setup commands */
        if (offset < sizeof(g2_expect)/sizeof(g2_expect[0])) {
            if (w != g2_expect[offset])
                return;
            continue;
        }
        if (w & 0300000)                /* not characters? */
            return;

        c = (w >> 7) & 0177;
        if (c)
            dp->buffer[dp->count++] = c;
        c = w & 0177;
        if (c)
            dp->buffer[dp->count++] = c;
    }
}

/* figure out what to send on TELNET connection
 * truncates new->count to the number sent so far
 * returns number of new characters sent
 */
static int g2out_send_new() {
    struct dspbuf *old = g2out_dspbufs + OLD;
    struct dspbuf *New = g2out_dspbufs + NEW;
    char *cp = New->buffer;
    int cur = 0;
    int start;

    /* nothing in newest refresh?
     * COULD have had undisplayed stuff on last screen before it was cleared??
     * would need to have a transmit queue??
     */
    if (New->count == 0)
        return 0;

    if (old->count &&                   /* have old chars */
        memcmp(old->buffer, New->buffer, old->count) == 0) { /* and a prefix */
        cur = old->count;
        cp += cur;
    }
    /* loop for chars while connected & tx enabled */
    start = cur;
    while (cur < New->count && g2_ldsc.conn && g2_ldsc.xmte) {
        if (g2out_putchar(*cp)) {
            cp++;
            cur++;
        }
    }
    New->count = cur;                   /* only remember what's been sent */
    return cur - start;                 /* remember number sent */
}

/****************************************************************
 * subsystem common routines (used by both G2IN and G2OUT SIMH DEVICEs)
 */

/* Reset routine */
t_stat g2_reset (DEVICE *dptr)
{
if (dptr->flags & DEV_DIS) {                            /* sync enables */
    g2in_dev.flags = g2in_dev.flags | DEV_DIS;
    g2out_dev.flags = g2out_dev.flags | DEV_DIS;
    }
else {
    g2in_dev.flags = g2in_dev.flags & ~DEV_DIS;
    g2out_dev.flags = g2out_dev.flags & ~DEV_DIS;
    }
if (g2in_unit.flags & UNIT_ATT)                         /* if attached, */
    sim_activate (&g2in_unit, tmxr_poll);               /* activate */
else sim_cancel (&g2in_unit);                           /* else stop */

g2kb_buf = 0;                                           /* clear buf */
g2kb_clr_done ();                                       /* clear done */

g2bb_bbuf = 0;                                          /* clear buttons */
g2bb_lbuf = 0;                                          /* clear lights */
g2bb_clr_flag ();

g2out_addr = 0;
g2out_clear();
sim_cancel (&g2out_unit);                                /* stop poll */
return SCPE_OK;
}

/* Attach master unit */
t_stat g2_attach (UNIT *uptr, CONST char *cptr)
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
