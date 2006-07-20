/* pdp11_dl.c: PDP-11 multiple terminal interface simulator

   Copyright (c) 1993-2006, Robert M Supnik

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

   ttix,ttox    DL11 terminal input/output
*/


#if defined (VM_PDP10)                                  /* PDP10 version */
#error "DL11 is not supported on the PDP-10!"

#elif defined (VM_VAX)                                  /* VAX version */
#error "DL11 is not supported on the VAX!"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif
#include "sim_sock.h"
#include "sim_tmxr.h"

#define TTX_MASK        (TTX_LINES - 1)

#define TTIXCSR_IMP     (CSR_DONE + CSR_IE)             /* terminal input */
#define TTIXCSR_RW      (CSR_IE)
#define TTIXBUF_ERR     0100000
#define TTIXBUF_OVR     0040000
#define TTIXBUF_RBRK    0020000
#define TTOXCSR_IMP     (CSR_DONE + CSR_IE)             /* terminal output */
#define TTOXCSR_RW      (CSR_IE)

extern int32 int_req[IPL_HLVL];
extern int32 tmxr_poll;

uint16 ttix_csr[TTX_LINES] = { 0 };                     /* control/status */
uint16 ttix_buf[TTX_LINES] = { 0 };
uint32 ttix_ireq = 0;
uint16 ttox_csr[TTX_LINES] = { 0 };                     /* control/status */
uint8 ttox_buf[TTX_LINES] = { 0 };
uint32 ttox_ireq = 0;
TMLN ttx_ldsc[TTX_LINES] = { 0 };                       /* line descriptors */
TMXR ttx_desc = { TTX_LINES, 0, 0, ttx_ldsc };          /* mux descriptor */

t_stat ttx_rd (int32 *data, int32 PA, int32 access);
t_stat ttx_wr (int32 data, int32 PA, int32 access);
t_stat ttx_reset (DEVICE *dptr);
t_stat ttix_svc (UNIT *uptr);
t_stat ttox_svc (UNIT *uptr);
t_stat ttx_attach (UNIT *uptr, char *cptr);
t_stat ttx_detach (UNIT *uptr);
t_stat ttx_summ (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat ttx_show (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat ttx_show_vec (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat ttx_set_lines (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat ttx_show_lines (FILE *st, UNIT *uptr, int32 val, void *desc);
void ttx_enbdis (int32 dis);
void ttix_clr_int (uint32 ln);
void ttix_set_int (int32 ln);
int32 ttix_iack (void);
void ttox_clr_int (int32 ln);
void ttox_set_int (int32 ln);
int32 ttox_iack (void);
void ttx_reset_ln (uint32 ln);

/* TTIX data structures

   ttix_dev      TTIX device descriptor
   ttix_unit     TTIX unit descriptor
   ttix_reg      TTIX register list
*/

DIB ttix_dib = {
    IOBA_TTIX, IOLN_TTIX, &ttx_rd, &ttx_wr,
    2, IVCL (TTIX), VEC_TTIX, { &ttix_iack, &ttox_iack }
    };

UNIT ttix_unit = { UDATA (&ttix_svc, 0, 0), KBD_POLL_WAIT };

REG ttix_reg[] = {
    { BRDATA (BUF, ttix_buf, DEV_RDX, 16, TTX_LINES) },
    { BRDATA (CSR, ttix_csr, DEV_RDX, 16, TTX_LINES) },
    { GRDATA (IREQ, ttix_ireq, DEV_RDX, TTX_LINES, 0) },
    { DRDATA (LINES, ttx_desc.lines, 6), REG_HRO },
    { GRDATA (DEVADDR, ttix_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, ttix_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

MTAB ttix_mod[] = {
    { UNIT_ATT, UNIT_ATT, "summary", NULL, NULL, &ttx_summ },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &ttx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &ttx_show, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &ttx_show, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
      &set_vec, &ttx_show_vec, NULL },
    { MTAB_XTD | MTAB_VDV, 0, "lines", "LINES",
      &ttx_set_lines, &ttx_show_lines },
    { 0 }
    };

DEVICE ttix_dev = {
    "TTIX", &ttix_unit, ttix_reg, ttix_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ttx_reset,
    NULL, &ttx_attach, &ttx_detach,
    &ttix_dib, DEV_UBUS | DEV_QBUS | DEV_DISABLE | DEV_DIS
    };

/* TTOX data structures

   ttox_dev      TTOX device descriptor
   ttox_unit     TTOX unit descriptor
   ttox_reg      TTOX register list
*/

UNIT ttox_unit[] = {
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT }
    };

REG ttox_reg[] = {
    { BRDATA (BUF, ttox_buf, DEV_RDX, 8, TTX_LINES) },
    { BRDATA (CSR, ttox_csr, DEV_RDX, 16, TTX_LINES) },
    { GRDATA (IREQ, ttox_ireq, DEV_RDX, TTX_LINES, 0) },
    { URDATA (TIME, ttox_unit[0].wait, 10, 31, 0,
              TTX_LINES, PV_LEFT) },
    { NULL }
    };

MTAB ttox_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &ttx_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &ttx_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &ttx_desc },
    { 0 }
    };

DEVICE ttox_dev = {
    "TTOX", ttox_unit, ttox_reg, ttox_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ttx_reset,
    NULL, NULL, NULL,
    NULL, DEV_UBUS | DEV_QBUS | DEV_DISABLE | DEV_DIS
    };

/* Terminal input routines */

t_stat ttx_rd (int32 *data, int32 PA, int32 access)
{
uint32 ln = ((PA - ttix_dib.ba) >> 3) & TTX_MASK;

switch ((PA >> 1) & 03) {                               /* decode PA<2:1> */

    case 00:                                            /* tti csr */
        *data = ttix_csr[ln] & TTIXCSR_IMP;
        return SCPE_OK;

    case 01:                                            /* tti buf */
        ttix_csr[ln] &= ~CSR_DONE;
        ttix_clr_int (ln);
        *data = ttix_buf[ln];
        return SCPE_OK;

    case 02:                                            /* tto csr */
        *data = ttox_csr[ln] & TTOXCSR_IMP;
        return SCPE_OK;

    case 03:                                            /* tto buf */
        *data = ttox_buf[ln];
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

t_stat ttx_wr (int32 data, int32 PA, int32 access)
{
uint32 ln = ((PA - ttix_dib.ba) >> 3) & TTX_MASK;

switch ((PA >> 1) & 03) {                               /* decode PA<2:1> */

    case 00:                                            /* tti csr */
        if (PA & 1) return SCPE_OK;
        if ((data & CSR_IE) == 0)
            ttix_clr_int (ln);
        else if ((ttix_csr[ln] & (CSR_DONE + CSR_IE)) == CSR_DONE)
            ttix_set_int (ln);
        ttix_csr[ln] = (uint16) ((ttix_csr[ln] & ~TTIXCSR_RW) | (data & TTIXCSR_RW));
        return SCPE_OK;

    case 01:                                            /* tti buf */
        return SCPE_OK;

    case 02:                                            /* tto csr */
        if (PA & 1) return SCPE_OK;
        if ((data & CSR_IE) == 0)
            ttox_clr_int (ln);
        else if ((ttox_csr[ln] & (CSR_DONE + CSR_IE)) == CSR_DONE)
            ttox_set_int (ln);
        ttox_csr[ln] = (uint16) ((ttox_csr[ln] & ~TTOXCSR_RW) | (data & TTOXCSR_RW));
        return SCPE_OK;

    case 03:                                            /* tto buf */
        if ((PA & 1) == 0)
            ttox_buf[ln] = data & 0377;
        ttox_csr[ln] &= ~CSR_DONE;
        ttox_clr_int (ln);
        sim_activate (&ttox_unit[ln], ttox_unit[ln].wait);
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

/* Terminal input service */

t_stat ttix_svc (UNIT *uptr)
{
int32 ln, c, temp;

if ((uptr->flags & UNIT_ATT) == 0) return SCPE_OK;      /* attached? */
sim_activate (uptr, tmxr_poll);                         /* continue poll */
ln = tmxr_poll_conn (&ttx_desc);                        /* look for connect */
if (ln >= 0) ttx_ldsc[ln].rcve = 1;                     /* got one? rcv enb */
tmxr_poll_rx (&ttx_desc);                               /* poll for input */
for (ln = 0; ln < TTX_LINES; ln++) {                    /* loop thru lines */
    if (ttx_ldsc[ln].conn) {                            /* connected? */
        if (temp = tmxr_getc_ln (&ttx_ldsc[ln])) {      /* get char */
            if (temp & SCPE_BREAK)                      /* break? */
                c = TTIXBUF_ERR|TTIXBUF_RBRK;
            else c = sim_tt_inpcvt (temp, TT_GET_MODE (ttox_unit[ln].flags));
            if (ttix_csr[ln] & CSR_DONE)
                c |= TTIXBUF_ERR|TTIXBUF_OVR;
            else {
                ttix_csr[ln] |= CSR_DONE;
                if (ttix_csr[ln] & CSR_IE) ttix_set_int (ln);
                }
            ttix_buf[ln] = c;
            }
        }
    }
return SCPE_OK;
}

/* Terminal output service */

t_stat ttox_svc (UNIT *uptr)
{
int32 c;
uint32 ln = uptr - ttox_unit;                           /* line # */

if (ttx_ldsc[ln].conn) {                                /* connected? */
    if (ttx_ldsc[ln].xmte) {                            /* tx enabled? */
        TMLN *lp = &ttx_ldsc[ln];                       /* get line */
        c = sim_tt_outcvt (ttox_buf[ln], TT_GET_MODE (ttox_unit[ln].flags));
        if (c >= 0) tmxr_putc_ln (lp, c);               /* output char */
        tmxr_poll_tx (&ttx_desc);                       /* poll xmt */
        }
    else {
        tmxr_poll_tx (&ttx_desc);                       /* poll xmt */
        sim_activate (uptr, ttox_unit[ln].wait);        /* wait */
        return SCPE_OK;
        }
    }
ttox_csr[ln] |= CSR_DONE;                               /* set done */
if (ttox_csr[ln] & CSR_IE) ttox_set_int (ln);
return SCPE_OK;
}

/* Interrupt routines */

void ttix_clr_int (uint32 ln)
{
ttix_ireq &= ~(1 << ln);                                /* clr mux rcv int */
if (ttix_ireq == 0) CLR_INT (TTIX);                     /* all clr? */
else SET_INT (TTIX);                                    /* no, set intr */
return;
}

void ttix_set_int (int32 ln)
{
ttix_ireq |= (1 << ln);                                 /* clr mux rcv int */
SET_INT (TTIX);                                         /* set master intr */
return;
}

int32 ttix_iack (void)
{
int32 ln;

for (ln = 0; ln < TTX_LINES; ln++) {                    /* find 1st line */
    if (ttix_ireq & (1 << ln)) {
        ttix_clr_int (ln);                              /* clear intr */
        return (ttix_dib.vec + (ln * 010));             /* return vector */
        }
    }
return 0;
}

void ttox_clr_int (int32 ln)
{
ttox_ireq &= ~(1 << ln);                                /* clr mux rcv int */
if (ttox_ireq == 0) CLR_INT (TTOX);                     /* all clr? */
else SET_INT (TTOX);                                    /* no, set intr */
return;
}

void ttox_set_int (int32 ln)
{
ttox_ireq |= (1 << ln);                                 /* clr mux rcv int */
SET_INT (TTOX);                                         /* set master intr */
return;
}

int32 ttox_iack (void)
{
int32 ln;

for (ln = 0; ln < TTX_LINES; ln++) {                    /* find 1st line */
    if (ttox_ireq & (1 << ln)) {
        ttox_clr_int (ln);                              /* clear intr */
        return (ttix_dib.vec + (ln * 010) + 4);         /* return vector */
        }
    }
return 0;
}

/* Reset */

t_stat ttx_reset (DEVICE *dptr)
{
int32 ln;

ttx_enbdis (dptr->flags & DEV_DIS);                     /* sync enables */
sim_cancel (&ttix_unit);                                /* assume stop */
if (ttix_unit.flags & UNIT_ATT)                         /* if attached, */
    sim_activate (&ttix_unit, tmxr_poll);               /* activate */
for (ln = 0; ln < TTX_LINES; ln++)                      /* for all lines */
    ttx_reset_ln (ln);
return auto_config (ttix_dev.name, ttx_desc.lines);     /* auto config */
}

/* Reset individual line */

void ttx_reset_ln (uint32 ln)
{
ttix_buf[ln] = 0;                                       /* clear buf, */
ttix_csr[ln] = CSR_DONE;
ttox_buf[ln] = 0;                                       /* clear buf */
ttox_csr[ln] = CSR_DONE;
sim_cancel (&ttox_unit[ln]);                            /* deactivate */
ttix_clr_int (ln);
ttox_clr_int (ln);
return;
}

/* Attach master unit */

t_stat ttx_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = tmxr_attach (&ttx_desc, uptr, cptr);                /* attach */
if (r != SCPE_OK) return r;                             /* error */
sim_activate (uptr, tmxr_poll);                         /* start poll */
return SCPE_OK;
}

/* Detach master unit */

t_stat ttx_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&ttx_desc, uptr);                      /* detach */
for (i = 0; i < TTX_LINES; i++)                         /* all lines, */
    ttx_ldsc[i].rcve = 0;                               /* disable rcv */
sim_cancel (uptr);                                      /* stop poll */
return r;
}

/* Show summary processor */

t_stat ttx_summ (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, t;

for (i = t = 0; i < TTX_LINES; i++) t = t + (ttx_ldsc[i].conn != 0);
if (t == 1) fprintf (st, "1 connection");
else fprintf (st, "%d connections", t);
return SCPE_OK;
}

/* SHOW CONN/STAT processor */

t_stat ttx_show (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, t;

for (i = t = 0; i < TTX_LINES; i++) t = t + (ttx_ldsc[i].conn != 0);
if (t) {
    for (i = 0; i < TTX_LINES; i++) {
        if (ttx_ldsc[i].conn) { 
            if (val) tmxr_fconns (st, &ttx_ldsc[i], i);
            else tmxr_fstats (st, &ttx_ldsc[i], i);
            }
        }
    }
else fprintf (st, "all disconnected\n");
return SCPE_OK;
}

/* Enable/disable device */

void ttx_enbdis (int32 dis)
{
if (dis) {
    ttix_dev.flags = ttox_dev.flags | DEV_DIS;
    ttox_dev.flags = ttox_dev.flags | DEV_DIS;
    }
else {
    ttix_dev.flags = ttix_dev.flags & ~DEV_DIS;
    ttox_dev.flags = ttox_dev.flags & ~DEV_DIS;
    }
return;
}

/* SHOW VECTOR processor */

t_stat ttx_show_vec (FILE *st, UNIT *uptr, int32 val, void *desc)
{
return show_vec (st, uptr, ttx_desc.lines * 2, desc);
}

/* Change number of lines */

t_stat ttx_set_lines (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 newln, i, t;
t_stat r;

if (cptr == NULL) return SCPE_ARG;
newln = get_uint (cptr, 10, TTX_LINES, &r);
if ((r != SCPE_OK) || (newln == ttx_desc.lines)) return r;
if (newln == 0) return SCPE_ARG;
if (newln < ttx_desc.lines) {
    for (i = newln, t = 0; i < ttx_desc.lines; i++) t = t | ttx_ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
        return SCPE_OK;
    for (i = newln; i < ttx_desc.lines; i++) {
        if (ttx_ldsc[i].conn) {
            tmxr_linemsg (&ttx_ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_reset_ln (&ttx_ldsc[i]);               /* reset line */
            }
        ttox_unit[i].flags |= UNIT_DIS;
        ttx_reset_ln (i);
        }
    }
else {
    for (i = ttx_desc.lines; i < newln; i++) {
        ttox_unit[i].flags &= ~UNIT_DIS;
        ttx_reset_ln (i);
        }
    }
ttx_desc.lines = newln;
return auto_config (ttix_dev.name, newln);             /* auto config */
}

/* Show number of lines */

t_stat ttx_show_lines (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "lines=%d", ttx_desc.lines);
return SCPE_OK;
}
