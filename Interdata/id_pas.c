/* id_pas.c: Interdata programmable async line adapter simulator

   Copyright (c) 2001-2013, Robert M Supnik

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

   pas          Programmable asynchronous line adapter(s)

   11-Oct-13    RMS     Poll PAS immediately to pick up initial connect
   18-Apr-12    RMS     Revised to use clock coscheduling
   21-Mar-12    RMS     Fixed TT_GET_MODE test to use TTUF_MODE_x (Michael Bloom)
   19-Nov-08    RMS     Revised for common TMXR show routines
   18-Jun-07    RMS     Added UNIT_IDLE flag
   18-Oct-06    RMS     Synced PASLA to clock
   22-Nov-05    RMS     Revised for new terminal processing routines
   29-Jun-05    RMS     Added SET PASLn DISCONNECT
   21-Jun-05    RMS     Fixed bug in SHOW CONN/STATS
   05-Jan-04    RMS     Revised for tmxr library changes
   09-May-03    RMS     Added network device flag

   This module implements up to 32 individual serial interfaces, representing
   either individual PASLA modules or combinations of the 2-line and 8-line
   multiplexors, which are functionally very similar. These interfaces are mapped
   to Telnet based connections as the lines of a terminal multiplexor.  The
   connection polling mechanism and the character input polling for all lines
   are done through a single polling job.
*/

#include "id_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#define PAS_LINES       32

#define UNIT_V_MDM      (TTUF_V_UF + 0)                 /* modem control */
#define UNIT_MDM        (1 << UNIT_V_MDM)

#define PASL_WAIT       500

/* Status byte */

#define STA_OVR         0x80                            /* overrun RO */
#define STA_PF          0x40                            /* parity err RONI */
#define STA_NCL2S       0x40                            /* not clr to snd XO */
#define STA_FR          0x20                            /* framing err RO */
#define STA_RCR         0x10                            /* rv chan rcv NI */
#define STA_CROF        0x02                            /* carrier off RO */
#define STA_RING        0x01                            /* ring RO */
#define STA_RCV         (STA_OVR|STA_PF|STA_FR|STA_RCR|STA_CROF|STA_RING)
#define SET_EX          (STA_OVR|STA_PF|STA_FR)
#define STA_XMT         (STA_BSY)

/* Command bytes 1,0 */

#define CMD_DTR         (0x20 << 8)                     /* DTR */
#define CMD_ECHO        (0x10 << 8)                     /* echoplex */
#define CMD_RCT         (0x08 << 8)                     /* RCT/DTB NI */
#define CMD_XMTB        (0x04 << 8)                     /* xmt break NI */
#define CMD_WRT         (0x02 << 8)                     /* write/read */
#define CMD_V_CLK       6                               /* baud rate */
#define CMD_M_CLK       0x3
#define CMD_V_DB        4                               /* data bits */
#define CMD_M_DB        0x3
#define CMD_STOP        0x80                            /* stop bit */
#define CMD_V_PAR       1                               /* parity */
#define CMD_M_PAR       0x3
#define GET_PAR(x)      (((x) >> CMD_V_PAR) & CMD_M_PAR)
#define PAR_NONE        0
#define PAR_RAW         1
#define PAR_ODD         2
#define PAR_EVEN        3

#define CMD_TYP         0x01                            /* command type */

extern uint32 int_req[INTSZ], int_enb[INTSZ];
extern int32 lfc_poll;

uint8 pas_sta[PAS_LINES];                               /* status */
uint16 pas_cmd[PAS_LINES];                              /* command */
uint8 pas_rbuf[PAS_LINES];                              /* rcv buf */
uint8 pas_xbuf[PAS_LINES];                              /* xmt buf */
uint8 pas_rarm[PAS_LINES];                              /* rcvr int armed */
uint8 pas_xarm[PAS_LINES];                              /* xmt int armed */
uint8 pas_rchp[PAS_LINES];                              /* rcvr chr pend */
uint8 pas_tplte[PAS_LINES * 2 + 1];                     /* template */

TMLN pas_ldsc[PAS_LINES] = { {0} };                     /* line descriptors */
TMXR pas_desc = { 8, 0, 0, pas_ldsc };                  /* mux descriptor */
#define PAS_ENAB        pas_desc.lines

uint32 pas (uint32 dev, uint32 op, uint32 dat);
void pas_ini (t_bool dtpl);
t_stat pasi_svc (UNIT *uptr);
t_stat paso_svc (UNIT *uptr);
t_stat pas_reset (DEVICE *dptr);
t_stat pas_attach (UNIT *uptr, char *cptr);
t_stat pas_detach (UNIT *uptr);
int32 pas_par (int32 cmd, int32 c);
t_stat pas_vlines (UNIT *uptr, int32 val, char *cptr, void *desc);
void pas_reset_ln (int32 i);

/* PAS data structures

   pas_dev      PAS device descriptor
   pas_unit     PAS unit descriptor
   pas_reg      PAS register list
   pas_mod      PAS modifiers list
*/

DIB pas_dib = { d_PAS, -1, v_PAS, pas_tplte, &pas, &pas_ini };

UNIT pas_unit = { UDATA (&pasi_svc, UNIT_ATTABLE|UNIT_IDLE, 0), 0 };

REG pas_reg[] = {
    { BRDATA (STA, pas_sta, 16, 8, PAS_LINES) },
    { BRDATA (CMD, pas_cmd, 16, 16, PAS_LINES) },
    { BRDATA (RBUF, pas_rbuf, 16, 8, PAS_LINES) },
    { BRDATA (XBUF, pas_xbuf, 16, 8, PAS_LINES) },
    { BRDATA (IREQ, &int_req[l_PAS], 16, 32, PAS_LINES / 16) },
    { BRDATA (IENB, &int_enb[l_PAS], 16, 32, PAS_LINES / 16) },
    { BRDATA (RARM, pas_rarm, 16, 1, PAS_LINES) },
    { BRDATA (XARM, pas_xarm, 16, 1, PAS_LINES) },
    { BRDATA (RCHP, pas_rchp, 16, 1, PAS_LINES) },
    { HRDATA (DEVNO, pas_dib.dno, 8), REG_HRO },
    { NULL }
    };

MTAB pas_mod[] = {
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, (void *) &pas_desc },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &pas_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &pas_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &pas_desc },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { MTAB_XTD | MTAB_VDV, 0, "LINES", "LINES",
      &pas_vlines, &tmxr_show_lines, (void *) &pas_desc },
    { 0 }
    };

DEVICE pas_dev = {
    "PAS", &pas_unit, pas_reg, pas_mod,
    1, 10, 31, 1, 16, 8,
    &tmxr_ex, &tmxr_dep, &pas_reset,
    NULL, &pas_attach, &pas_detach,
    &pas_dib, DEV_MUX | DEV_DISABLE
    };

/* PASL data structures

   pasl_dev     PASL device descriptor
   pasl_unit    PASL unit descriptor
   pasl_reg     PASL register list
   pasl_mod     PASL modifiers list
*/

UNIT pasl_unit[] = {
    { UDATA (&paso_svc, 0, 0), PASL_WAIT },             /* all but 8 dis */
    { UDATA (&paso_svc, 0, 0), PASL_WAIT },
    { UDATA (&paso_svc, 0, 0), PASL_WAIT },
    { UDATA (&paso_svc, 0, 0), PASL_WAIT },
    { UDATA (&paso_svc, 0, 0), PASL_WAIT },
    { UDATA (&paso_svc, 0, 0), PASL_WAIT },
    { UDATA (&paso_svc, 0, 0), PASL_WAIT },
    { UDATA (&paso_svc, 0, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT },
    { UDATA (&paso_svc, UNIT_DIS, 0), PASL_WAIT }
    };

MTAB pasl_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { UNIT_MDM, 0, "no dataset", "NODATASET", NULL },
    { UNIT_MDM, UNIT_MDM, "dataset", "DATASET", NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &pas_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &pas_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &pas_desc },
    { 0 }
    };

REG pasl_reg[] = {
    { URDATA (TIME, pasl_unit[0].wait, 16, 24, 0,
              PAS_LINES, REG_NZ + PV_LEFT) },
    { NULL }
    };

DEVICE pasl_dev = {
    "PASL", pasl_unit, pasl_reg, pasl_mod,
    PAS_LINES, 10, 31, 1, 16, 8,
    NULL, NULL, &pas_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* PAS: IO routine */

uint32 pas (uint32 dev, uint32 op, uint32 dat)
{
int32 ln = (dev - pas_dib.dno) >> 1;
int32 xmt = (dev - pas_dib.dno) & 1;
int32 t, old_cmd;

switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        return BY;                                      /* byte only */

    case IO_RD:                                         /* read */
        pas_rchp[ln] = 0;                               /* clr chr pend */
        pas_sta[ln] = pas_sta[ln] & ~STA_OVR;           /* clr overrun */
        return pas_rbuf[ln];                            /* return buf */

    case IO_WD:                                         /* write */
        pas_xbuf[ln] = dat & 0xFF;                      /* store char */
        pas_sta[ln] = pas_sta[ln] | STA_BSY;            /* set busy */
        sim_activate (&pasl_unit[ln], pasl_unit[ln].wait);
        break;

    case IO_SS:                                         /* status */
        if (xmt) {                                      /* xmt side? */
            if (pas_ldsc[ln].conn == 0)                 /* not conn? */
                t = STA_NCL2S | STA_BSY;                /* busy, not clr */
            else t = pas_sta[ln] & STA_XMT;             /* else just busy */
            }
        else {
            t = pas_sta[ln] & STA_RCV;                  /* get static */
            if (!pas_rchp[ln])                          /* no char? busy */
                t = t | STA_BSY;
            if (pas_ldsc[ln].conn == 0)                 /* not connected? */
                t = t | STA_BSY | STA_EX;               /* = !dsr */
            if (t & SET_EX)                             /* test for ex */
                t = t | STA_EX;
            }
        return t;

    case IO_OC:                                         /* command */
        old_cmd = pas_cmd[ln];                          /* old cmd */
        if (dat & CMD_TYP) {                            /* type 1? */
            pas_cmd[ln] = (pas_cmd[ln] & 0xFF) | (dat << 8);
            if (pas_cmd[ln] & CMD_WRT)                  /* write? */
                pas_xarm[ln] = int_chg (v_PASX + ln + ln, dat, pas_xarm[ln]);
            else pas_rarm[ln] = int_chg (v_PAS + ln + ln, dat, pas_rarm[ln]);
            }
        else pas_cmd[ln] = (pas_cmd[ln] & ~0xFF) | dat;
        if (pasl_unit[ln].flags & UNIT_MDM) {           /* modem ctrl? */
            if ((pas_cmd[ln] & CMD_DTR) && (pas_sta[ln] & STA_RING))
                pas_sta[ln] = pas_sta[ln] & ~(STA_CROF | STA_RING);
            if (old_cmd & ~pas_cmd[ln] & CMD_DTR) {
                tmxr_linemsg (&pas_ldsc[ln], "\r\nLine hangup\r\n");
                tmxr_reset_ln (&pas_ldsc[ln]);          /* reset line */
                pas_sta[ln] = pas_sta[ln] | STA_CROF;   /* no carrier */
                if (pas_rarm[ln])
                    SET_INT (v_PAS + ln + ln);
                }
            }
        break;
        }

return 0;
}

/* Unit service - receive side

   Poll all active lines for input
   Poll for new connections
*/

t_stat pasi_svc (UNIT *uptr)
{
int32 ln, c, out;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_OK;
sim_activate (uptr, lfc_cosched (lfc_poll));            /* continue poll */
ln = tmxr_poll_conn (&pas_desc);                        /* look for connect */
if (ln >= 0) {                                          /* got one? */
    if ((pasl_unit[ln].flags & UNIT_MDM) &&             /* modem control */
        ((pas_cmd[ln] & CMD_DTR) == 0))                 /* & !dtr? */
        pas_sta[ln] = pas_sta[ln] | STA_RING | STA_CROF; /* set ring, no cd */
    else pas_sta[ln] = pas_sta[ln] & ~STA_CROF;         /* just answer */
    if (pas_rarm[ln])                                   /* interrupt */
        SET_INT (v_PAS + ln + ln);
    pas_ldsc[ln].rcve = 1;                              /* rcv enabled */ 
    }
tmxr_poll_rx (&pas_desc);                               /* poll for input */
for (ln = 0; ln < PAS_ENAB; ln++) {                     /* loop thru lines */
    if (pas_ldsc[ln].conn) {                            /* connected? */
        if ((c = tmxr_getc_ln (&pas_ldsc[ln]))) {       /* any char? */
            pas_sta[ln] = pas_sta[ln] & ~(STA_FR | STA_PF);
            if (pas_rchp[ln])
                pas_sta[ln] = pas_sta[ln] | STA_OVR;
            if (pas_rarm[ln])
                SET_INT (v_PAS + ln + ln);
            if (c & SCPE_BREAK) {                       /* break? */
                pas_sta[ln] = pas_sta[ln] | STA_FR;     /* framing error */
                pas_rbuf[ln] = 0;                       /* no character */
                }
            else {                                      /* normal */
                out = c & 0x7F;                         /* echo is 7b */
                c = sim_tt_inpcvt (c, TT_GET_MODE (pasl_unit[ln].flags));
                if (TT_GET_MODE (pasl_unit[ln].flags) != TTUF_MODE_8B)
                    c = pas_par (pas_cmd[ln], c);       /* apply parity */
                pas_rbuf[ln] = c;                       /* save char */
                pas_rchp[ln] = 1;                       /* char pending */
                if ((pas_cmd[ln] & CMD_ECHO) && pas_ldsc[ln].xmte) {
                    TMLN *lp = &pas_ldsc[ln];           /* get line */
                    out = sim_tt_outcvt (out, TT_GET_MODE (pasl_unit[ln].flags));
                    if (out >= 0)                       /* output char */
                        tmxr_putc_ln (lp, out);
                    tmxr_poll_tx (&pas_desc);           /* poll xmt */
                    }
                }                                       /* end else normal */
            }                                           /* end if char */
        }                                               /* end if conn */
    else if ((pas_sta[ln] & STA_CROF) == 0) {           /* not conn, was conn? */
        pas_sta[ln] = pas_sta[ln] | STA_CROF;           /* no carrier */
        if (pas_rarm[ln])                               /* intr */
            SET_INT (v_PAS + ln + ln);
        }
    }                                                   /* end for */
return SCPE_OK;
}

/* Unit service - transmit side */

t_stat paso_svc (UNIT *uptr)
{
int32 c;
uint32 ln = uptr - pasl_unit;                           /* line # */

if (pas_ldsc[ln].conn) {                                /* connected? */
    if (pas_ldsc[ln].xmte) {                            /* xmt enabled? */
        TMLN *lp = &pas_ldsc[ln];                       /* get line */
        if (TT_GET_MODE (pasl_unit[ln].flags) == TTUF_MODE_8B)
            c = pas_par (pas_cmd[ln], pas_xbuf[ln]);    /* apply parity */
        else c = sim_tt_outcvt (pas_xbuf[ln], TT_GET_MODE (pasl_unit[ln].flags));
        if (c >= 0) {
            tmxr_putc_ln (lp, c);                       /* output char */
            }
        tmxr_poll_tx (&pas_desc);                       /* poll xmt */
        }
    else {                                              /* buf full */
        tmxr_poll_tx (&pas_desc);                       /* poll xmt */
        sim_activate (uptr, pasl_unit[ln].wait);        /* wait */
        return SCPE_OK;
        }
    }
pas_sta[ln] = pas_sta[ln] & ~STA_BSY;                   /* not busy */
if (pas_xarm[ln])                                       /* set intr */
    SET_INT (v_PASX + ln + ln);
return SCPE_OK;
}

int32 pas_par (int32 cmd, int32 c)
{
int32 pf = GET_PAR (cmd);
static const uint8 odd_par[] = {
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* 00 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* 10 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* 20 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* 30 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* 40 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* 50 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* 60 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* 70 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* 80 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* 90 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* A0 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* B0 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* C0 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* D0 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* E0 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* F0 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80
    };

switch (pf) {                                           /* case on parity */

    case PAR_ODD:
        return (odd_par[c & 0x7F]) | (c & 0x7F);

    case PAR_EVEN:
        return (odd_par[c & 0x7F] ^ 0x80) | (c & 0x7F);

    case PAR_NONE:
    case PAR_RAW:
        break;
        }

return c & 0xFF;
}

/* Reset routine */

t_stat pas_reset (DEVICE *dptr)
{
int32 i;

if (dptr->flags & DEV_DIS) {                            /* disabled? */
    pas_dev.flags = pas_dev.flags | DEV_DIS;            /* disable lines */
    pasl_dev.flags = pasl_dev.flags | DEV_DIS;
    }
else {
    pas_dev.flags = pas_dev.flags & ~DEV_DIS;           /* enable lines */
    pasl_dev.flags = pasl_dev.flags & ~DEV_DIS;
    }
if (pas_unit.flags & UNIT_ATT)                          /* master att? */
    sim_activate (&pas_unit, lfc_poll);
else sim_cancel (&pas_unit);                            /* else stop */
for (i = 0; i < PAS_LINES; i++)
    pas_reset_ln (i);
return SCPE_OK;
}

/* Attach master unit */

t_stat pas_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = tmxr_attach (&pas_desc, uptr, cptr);                /* attach */
if (r != SCPE_OK)                                       /* error */
    return r;
sim_activate (uptr, 0);                                 /* immediate poll */
return SCPE_OK;
}

/* Detach master unit */

t_stat pas_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&pas_desc, uptr);                      /* detach */
for (i = 0; i < PAS_LINES; i++)                         /* disable rcv */
    pas_ldsc[i].rcve = 0;
sim_cancel (uptr);                                      /* stop poll */
return r;
}

/* Change number of lines */

t_stat pas_vlines (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 newln, i, t;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newln = get_uint (cptr, 10, PAS_LINES, &r);
if ((r != SCPE_OK) || (newln == PAS_ENAB))
    return r;
if (newln == 0)
    return SCPE_ARG;
if (newln < PAS_ENAB) {
    for (i = newln, t = 0; i < PAS_ENAB; i++)
        t = t | pas_ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
        return SCPE_OK;
    for (i = newln; i < PAS_ENAB; i++) {
        if (pas_ldsc[i].conn) {
            tmxr_linemsg (&pas_ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_reset_ln (&pas_ldsc[i]);               /* reset line */
			}
        pasl_unit[i].flags = pasl_unit[i].flags | UNIT_DIS;
        pas_reset_ln (i);
        }
    }
else {
    for (i = PAS_ENAB; i < newln; i++) {
        pasl_unit[i].flags = pasl_unit[i].flags & ~UNIT_DIS;
        pas_reset_ln (i);
        }
    }
PAS_ENAB = newln;
return SCPE_OK;
}

/* Reset an individual line */

void pas_reset_ln (int32 i)
{
CLR_INT (v_PAS + i + i);                                /* clear int */
CLR_ENB (v_PAS + i + i);
CLR_INT (v_PASX + i + i);                               /* disable int */
CLR_ENB (v_PASX + i + i);
pas_rarm[i] = pas_xarm[i] = 0;                          /* disarm int */
pas_rbuf[i] = pas_xbuf[i] = 0;                          /* clear state */
pas_cmd[i] = 0;
pas_rchp[i] = 0;
pas_sta[i] = 0;
if (pas_ldsc[i].conn == 0)                              /* clear carrier */
    pas_sta[i] = pas_sta[i] | STA_CROF;
sim_cancel (&pasl_unit[i]);
return;
}

/* Init template */

void pas_ini (t_bool dtpl)
{
int32 i, j;

for (i = j = 0; i < PAS_ENAB; i++) {
    pas_tplte[j] = j;
    pas_tplte[j + 1] = j + o_PASX;
    j = j + 2;
    }
pas_tplte[j] = TPL_END;
return;
}
