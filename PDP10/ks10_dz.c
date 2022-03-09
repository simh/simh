/* ks10_dz.c: PDP-10 DZ11 communication server simulator

   Copyright (c) 2021, Richard Cornwell

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

#ifndef NUM_DEVS_DZ
#define NUM_DEVS_DZ 0
#endif

#if (NUM_DEVS_DZ > 0)


#define DZ11_LINES    (8 * NUM_DEVS_DZ)

/* 0 CSR */

#define MAINT    0000010         /* Maintance mode */
#define CLR      0000020
#define MSE      0000040
#define RIE      0000100
#define RDONE    0000200
#define TLINE    0003400
#define TLINE_V  8
#define SAE      0010000
#define SA       0020000
#define TIE      0040000
#define TRDY     0100000

/* 2 RBUF */
#define RBUF     0000377
#define RXLINE   0003400
#define RXLINE_V 8
#define PAR_ERR  0010000
#define FRM_ERR  0020000
#define OVRN     0040000
#define VALID    0100000

/* 2 LPR */
#define LINE     0000007
#define CHAR_LEN 0000030
#define STOP     0000040
#define PAR_ENB  0000100
#define ODD_PAR  0000200
#define FREQ     0007400
#define RXON     0010000

/* 4 TCR */
#define LINE_ENB 0000001
#define DTR      0000400

/* 6 MSR */
#define RO       0000001
#define CO       0000400

/* 6 TDR */
#define TBUF     0000377
#define BRK      0000400

struct _buffer {
    int      in_ptr;     /* Insert pointer */
    int      out_ptr;    /* Remove pointer */
    uint16   buff[64];   /* Buffer */
    int      len;        /* Length */
};

#define full(q)       ((((q)->in_ptr + 1) & 0x3f) == (q)->out_ptr)
#define empty(q)      ((q)->in_ptr == (q)->out_ptr)
#define not_empty(q)  ((q)->in_ptr != (q)->out_ptr)
#define inco(q)       (q)->out_ptr = ((q)->out_ptr + 1) & 0x3f
#define inci(q)       (q)->in_ptr = ((q)->in_ptr + 1) & 0x3f

#define LINE_EN   01
#define DTR_FLAG  02

uint16         dz_csr[NUM_DEVS_DZ];
uint16         dz_xmit[DZ11_LINES];
uint8          dz_flags[DZ11_LINES];
uint8          dz_ring[NUM_DEVS_DZ];
struct _buffer dz_recv[NUM_DEVS_DZ];
TMLN           dz_ldsc[DZ11_LINES] = { 0 };     /* Line descriptors */
TMXR           dz_desc = { DZ11_LINES, 0, 0, dz_ldsc };
extern int32 tmxr_poll;

int    dz_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access);
int    dz_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access);
t_stat dz_svc (UNIT *uptr);
t_stat dz_reset (DEVICE *dptr);
void   dz_checkirq(struct pdp_dib   *dibp);
uint16 dz_vect(struct pdp_dib *dibp);
t_stat dz_set_modem (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dz_show_modem (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dz_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dz_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dz_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dz_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dz_attach (UNIT *uptr, CONST char *cptr);
t_stat dz_detach (UNIT *uptr);
t_stat dz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr);
const char *dz_description (DEVICE *dptr);
DIB dz_dib = { 0760000, 077, 0340, 5, 3, &dz_read, &dz_write, 0, 0, 0 };

UNIT dz_unit = {
    UDATA (&dz_svc, TT_MODE_7B+UNIT_IDLE+UNIT_DISABLE+UNIT_ATTABLE, 0), KBD_POLL_WAIT
    };

REG dz_reg[] = {
    { DRDATA (TIME, dz_unit.wait, 24), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB dz_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", NULL },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  NULL },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  NULL },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  NULL },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "addr", "addr",  &uba_set_addr, uba_show_addr,
              NULL, "Sets address of DZ11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "vect", "vect",  &uba_set_vect, uba_show_vect,
              NULL, "Sets vect of DZ11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "br", "br",  &uba_set_br, uba_show_br,
              NULL, "Sets br of DZ11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "ctl", "ctl",  &uba_set_ctl, uba_show_ctl,
              NULL, "Sets uba of DZ11" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &dz_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &dz_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dz_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dz_desc, "Display multiplexer statistics" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dz_setnl, &tmxr_show_lines, (void *) &dz_desc, "Set number of lines" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "LOG=n=file",
        &dz_set_log, NULL, (void *)&dz_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "NOLOG",
        &dz_set_nolog, NULL, (void *)&dz_desc, "Disable logging on designated line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "LOG", NULL,
        NULL, &dz_show_log, (void *)&dz_desc, "Display logging for all lines" },

    { 0 }
    };

DEVICE dz_dev = {
    "DZ", &dz_unit, dz_reg, dz_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &dz_reset,
    NULL, &dz_attach, &dz_detach,
    &dz_dib, DEV_MUX | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dz_help, NULL, NULL, &dz_description
    };


int
dz_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access)
{
    struct pdp_dib   *dibp = (DIB *)dptr->ctxt;
    int               base;
    uint16            temp;
    int               ln;
    TMLN             *lp;
    int               i;

    if ((dptr->flags & DEV_DIS) != 0)
        return 1;
    if ((dptr->units[0].flags & UNIT_DIS) != 0)
        return 1;
    addr &= dibp->uba_mask;
    if (addr < 010 || addr > 047)
        return 1;
    base = ((addr & 070) - 010) >> 3;
    if (((base + 1) * 8) > dz_desc.lines)
        return 1;
    sim_debug(DEBUG_DETAIL, dptr, "DZ%o write %06o %06o %o\n", base,
             addr, data, access);

    switch (addr & 06) {
    case 0:
            if (access == BYTE) {
                temp = dz_csr[base];
                if (addr & 1)
                    data = data | (temp & 0377);
                else
                    data = (temp & 0177400) | data;
            }
            if (data & CLR) {
                dz_csr[base] = 0;
                dz_recv[base].in_ptr = dz_recv[base].out_ptr = 0;
                dz_recv[base].len = 0;
                /* Set up the current status */
                ln = base << 3;
                for (i = 0; i < 8; i++) {
                    dz_flags[ln + i] &= ~LINE_EN;
                }
                return 0;
            }
            dz_csr[base] &= ~(TIE|SAE|RIE|MSE|CLR|MAINT);
            dz_csr[base] |= data & (TIE|SAE|RIE|MSE|MAINT);
            if (((dz_csr[base] & (RDONE|RIE)) == (RDONE|RIE)) ||
                (dz_csr[base] & (SA|SAE)) == (SA|SAE))
               uba_set_irq(dibp, dibp->uba_vect + (010 * base));
            else
               uba_clr_irq(dibp, dibp->uba_vect + (010 * base));
            if ((dz_csr[base] & (TRDY|TIE)) == (TRDY|TIE))
               uba_set_irq(dibp, dibp->uba_vect + 4 + (010 * base));
            else
               uba_clr_irq(dibp, dibp->uba_vect + 4 +  (010 * base));
            break;

    case 2:
            ln = (data & 07) + (base << 3);
            dz_ldsc[ln].rcve = (data & RXON) != 0;
            break;

    case 4:
            temp = 0;
            ln = base << 3;
            /* Set up the current status */
            for (i = 0; i < 8; i++) {
                if (dz_flags[ln + i] & LINE_EN)
                    temp |= LINE_ENB << i;
                if (dz_flags[ln + i] & DTR_FLAG)
                    temp |= DTR << i;
                dz_flags[ln + i] = 0;
            }
            if (access == BYTE) {
                if (addr & 1)
                    data = data | (temp & 0377);
                else
                    data = (temp & 0177400) | data;
            }
            dz_csr[base] &= ~(TRDY);
            for (i = 0; i < 8; i++) {
                lp = &dz_ldsc[ln + i];
                if ((data & (LINE_ENB << i)) != 0)
                    dz_flags[ln + i] |= LINE_EN;
                if ((data & (DTR << i)) != 0)
                    dz_flags[ln + i] |= DTR_FLAG;
                if (dz_flags[ln + i] & DTR_FLAG)
                    tmxr_set_get_modem_bits(lp, TMXR_MDM_OUTGOING, 0, NULL);
                else
                    tmxr_set_get_modem_bits(lp, 0, TMXR_MDM_OUTGOING, NULL);
       sim_debug(DEBUG_EXP, dptr, "DZ%o sstatus %07o %o %o\n", base, data, i, dz_flags[ln+i]);
            }
            uba_clr_irq(dibp, dibp->uba_vect + 4 +  (010 * base));
            break;

    case 6:
            if (access == BYTE && (addr & 1) != 0) {
                break;
            }

            if ((dz_csr[base] & TRDY) == 0)
                break;

            ln = ((dz_csr[base] & TLINE) >> TLINE_V) + (base << 3);
            lp = &dz_ldsc[ln];

            if ((dz_flags[ln] & LINE_EN) != 0 && lp->conn) {
                int32  ch = data & 0377;
                t_stat r;
                ch = sim_tt_outcvt(ch, TT_GET_MODE (dz_unit.flags) | TTUF_KSR);
                /* Try and send character */
                r = tmxr_putc_ln(lp, ch);
                /* If character did not send, queue it */
                if (r == SCPE_STALL)
                    dz_xmit[ln] = TRDY | ch;
             }
             dz_csr[base] &= ~TRDY;
             uba_clr_irq(dibp, dibp->uba_vect + 4 +  (010 * base));
             break;
    }

    dz_checkirq(dibp);
    return 0;
}

int
dz_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access)
{
    struct pdp_dib   *dibp = (DIB *)dptr->ctxt;
    int               base;
    uint16            temp;
    int               ln;
    TMLN             *lp;
    int               i;

    if ((dptr->flags & DEV_DIS) != 0)
        return 1;
    if ((dptr->units[0].flags & UNIT_DIS) != 0)
        return 1;
    addr &= dibp->uba_mask;
    if (addr < 010 || addr > 047)
        return 1;
    base = ((addr & 070) - 010) >> 3;
    if (((base + 1) * 8) > dz_desc.lines)
        return 1;
    switch (addr & 06) {
    case 0:
            *data = dz_csr[base];
            break;

    case 2:
            *data = 0;
            if ((dz_csr[base] & MSE) == 0)
                return 0;
            dz_csr[base] &= ~(SA|RDONE);
            uba_clr_irq(dibp, dibp->uba_vect + (010 * base));
            if (!empty(&dz_recv[base])) {
                *data = dz_recv[base].buff[dz_recv[base].out_ptr];
                inco(&dz_recv[base]);
                dz_recv[base].len = 0;
            }
            if (!empty(&dz_recv[base])) {
                dz_csr[base] |= RDONE;
                if (dz_csr[base] & RIE) {
                   uba_set_irq(dibp, dibp->uba_vect + (010 * base));
                }
            }
            break;

    case 4:
            temp = 0;
            base <<= 3;
            /* Set up the current status */
            for (ln = 0; ln < 8; ln++) {
                if (dz_flags[base + ln] & LINE_EN)
                    temp |= LINE_ENB << ln;
                if (dz_flags[base + ln] & DTR_FLAG)
                    temp |= DTR << ln;
            }
            *data = temp;
            break;

     case 6:
            temp = (uint16)dz_ring[base];
            ln = base << 3;
            for (i = 0; i < 8; i++) {
                lp = &dz_ldsc[ln + i];
                if (lp->conn)
                    temp |= CO << i;
            }
            dz_ring[base] = 0;
            *data = temp;
            break;
     }
     sim_debug(DEBUG_DETAIL, dptr, "DZ%o read %06o %06o %o\n", base,
             addr, *data, access);
     return 0;
}

/* Unit service */
t_stat dz_svc (UNIT *uptr)
{
    int32             ln;
    int               base;
    uint16            temp;
    DEVICE           *dptr = find_dev_from_unit (uptr);
    struct pdp_dib   *dibp = (DIB *)dptr->ctxt;
    TMLN             *lp;

    if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
        return SCPE_OK;
    ln = tmxr_poll_conn (&dz_desc);                     /* look for connect */
    if (ln >= 0) {                                      /* got one? rcv enb*/
        dz_ring[(ln & 030) >> 3] |= (1 << (ln & 7));
        sim_debug(DEBUG_DETAIL, &dz_dev, "DZ line connect %d\n", ln);
        dz_xmit[ln] = 0;
    }
    tmxr_poll_tx(&dz_desc);
    tmxr_poll_rx(&dz_desc);
    for (ln = 0; ln < dz_desc.lines; ln++) {
        lp = &dz_ldsc[ln];
        base = (ln >> 3) & 03;
        if (dz_xmit[ln] != 0) {
            /* Try and send character */
            t_stat r = tmxr_putc_ln(lp, dz_xmit[ln] & 0377);
            /* If character did not send, queue it */
            if (r == SCPE_OK)
                dz_xmit[ln] = 0;
        }
        /* If silo full, skip to next */
        while (!full(&dz_recv[base])) {
            int32 ch = tmxr_getc_ln(lp);
            if ((ch & TMXR_VALID) != 0) {
               if (ch & SCPE_BREAK) {                    /* break? */
                    temp = FRM_ERR;
                } else {
                    ch = sim_tt_inpcvt (ch, TT_GET_MODE(dz_unit.flags) | TTUF_KSR);
                    temp = VALID | ((ln & 07) << RXLINE_V) | (uint16)(ch & RBUF);
                }
                dz_recv[base].buff[dz_recv[base].in_ptr] = temp;
                inci(&dz_recv[base]);
                dz_recv[base].len++;
                dz_csr[base] |= RDONE;
                if (dz_csr[base] & RIE)
                    uba_set_irq(dibp, dibp->uba_vect + (010 * base));
                if (dz_recv[base].len > 16) {
                    dz_csr[base] |= SA;
                    if (dz_csr[base] & SAE) {
                       uba_set_irq(dibp, dibp->uba_vect + (010 * base));
                    }
                }
                sim_debug(DEBUG_DETAIL, dptr, "TTY recieve %d: %o\n", ln, ch);
            } else
                break;
        }
    }

    dz_checkirq(dibp);
    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */
    return SCPE_OK;
}

/* Check if a device has an IRQ event ready */
void
dz_checkirq(struct pdp_dib   *dibp)
{
    int        i;
    int        ln;
    int        stop;
    TMLN      *lp;
    int        irq = 0;

    for (i = 0; i < NUM_DEVS_DZ; i++) {
         if ((dz_csr[i] & MSE) == 0)
             continue;
         ln = ((dz_csr[i] & TLINE) >> TLINE_V) + (i << 3);
         stop = ln;
         if ((dz_csr[i] & TRDY) == 0) {
             /* See if there is another line ready */
             do {
                 ln = (ln & 070) | ((ln + 1) & 07);
                 lp = &dz_ldsc[ln];
                 /* Connected and empty xmit_buffer */
                 if ((dz_flags[ln] & LINE_EN) != 0 && dz_xmit[ln] == 0) {
                     sim_debug(DEBUG_DETAIL, &dz_dev, "DZ line ready %o\n", ln);

                     dz_csr[i] &= ~(TRDY|TLINE);
                     dz_csr[i] |= TRDY | ((ln & 07) << TLINE_V);
                     if (dz_csr[i] & TIE) {
                        uba_set_irq(dibp, dibp->uba_vect + 4 + (010 * i));
                     }
                     break;
                 }
             } while (ln != stop);
         }
    }
}

/* Reset routine */

t_stat
dz_reset (DEVICE *dptr)
{
    int      i;

    if (dz_unit.flags & UNIT_ATT)                           /* if attached, */
        sim_activate (&dz_unit, tmxr_poll);                 /* activate */
    else
        sim_cancel (&dz_unit);                             /* else stop */
    for (i = 0; i < NUM_DEVS_DZ; i++) {
        dz_csr[i] = 0;
        dz_recv[i].in_ptr = dz_recv[i].out_ptr = 0;
        dz_recv[i].len = 0;
        dz_ring[i] = 0;
        dz_xmit[i] = 0;
    }
    for (i = 0; i < DZ11_LINES; i++) {
         dz_flags[i] = 0;
    }
    return SCPE_OK;
}


/* SET LINES processor */

t_stat
dz_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 newln, i, t;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    newln = (int32) get_uint (cptr, 10, DZ11_LINES, &r);
    if ((r != SCPE_OK) || (newln == dz_desc.lines))
        return r;
    if ((newln == 0) || (newln > DZ11_LINES) || (newln % 8) != 0)
        return SCPE_ARG;
    if (newln < dz_desc.lines) {
        for (i = newln - 1, t = 0; i < dz_desc.lines; i++)
            t = t | dz_ldsc[i].conn;
        if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
            return SCPE_OK;
        for (i = newln - 1; i < dz_desc.lines; i++) {
            if (dz_ldsc[i].conn) {
                tmxr_linemsg (&dz_ldsc[i], "\r\nOperator disconnected line\r\n");
                tmxr_send_buffered_data (&dz_ldsc[i]);
            }
            tmxr_detach_ln (&dz_ldsc[i]);               /* completely reset line */
        }
    }
    if (dz_desc.lines < newln)
        memset (dz_ldsc + dz_desc.lines, 0, sizeof(*dz_ldsc)*(newln-dz_desc.lines));
    dz_desc.lines = newln;
    return dz_reset (&dz_dev);                         /* setup lines and auto config */
}

/* SET LOG processor */

t_stat
dz_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    char gbuf[CBUFSIZE];
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    cptr = get_glyph (cptr, gbuf, '=');
    if ((cptr == NULL) || (*cptr == 0) || (gbuf[0] == 0))
        return SCPE_ARG;
    ln = (int32) get_uint (gbuf, 10, dz_desc.lines, &r);
    if ((r != SCPE_OK) || (ln > dz_desc.lines))
        return SCPE_ARG;
    return tmxr_set_log (NULL, ln, cptr, desc);
}

/* SET NOLOG processor */

t_stat
dz_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    ln = (int32) get_uint (cptr, 10, dz_desc.lines, &r);
    if ((r != SCPE_OK) || (ln > dz_desc.lines))
        return SCPE_ARG;
    return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

t_stat dz_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32 i;

    for (i = 0; i < dz_desc.lines; i++) {
        fprintf (st, "line %d: ", i);
        tmxr_show_log (st, NULL, i, desc);
        fprintf (st, "\n");
        }
    return SCPE_OK;
}


/* Attach routine */

t_stat
dz_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat reason;

    reason = tmxr_attach (&dz_desc, uptr, cptr);
    if (reason != SCPE_OK)
      return reason;
    sim_activate (uptr, tmxr_poll);
    return SCPE_OK;
}

/* Detach routine */

t_stat
dz_detach (UNIT *uptr)
{
    int32  i;
    t_stat reason;
    reason = tmxr_detach (&dz_desc, uptr);
    for (i = 0; i < dz_desc.lines; i++)
        dz_ldsc[i].rcve = 0;
    sim_cancel (uptr);
    return reason;
}

t_stat dz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "DZ11 Terminal Interfaces\n\n");
fprintf (st, "Each DZ11 supports 8 serial lines. Up to 32 can be configured\n");
fprintf (st, "   sim> SET DZ LINES=n          set number of additional lines to n [8-32]\n\n");
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
fprintf (st, "Finally, each line supports output logging.  The SET DZn LOG command enables\n");
fprintf (st, "logging on a line:\n\n");
fprintf (st, "   sim> SET DZn LOG=filename   log output of line n to filename\n\n");
fprintf (st, "The SET DZn NOLOG command disables logging and closes the open log file,\n");
fprintf (st, "if any.\n\n");
fprintf (st, "Once DZ is attached and the simulator is running, the terminals listen for\n");
fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
fprintf (st, "by the Telnet client, a SET DZ DISCONNECT command, or a DETACH DC command.\n\n");
fprintf (st, "Other special commands:\n\n");
fprintf (st, "   sim> SHOW DZ CONNECTIONS    show current connections\n");
fprintf (st, "   sim> SHOW DZ STATISTICS     show statistics for active connections\n");
fprintf (st, "   sim> SET DZn DISCONNECT     disconnects the specified line.\n\n");
fprintf (st, "The DZ11 is a unibus device, various parameters can be changed on these devices\n");
fprintf (st, "\n The address of the device can be set with: \n");
fprintf (st, "      sim> SET DZ ADDR=octal   default address= 760000\n");
fprintf (st, "\n The interrupt vector can be set with: \n");
fprintf (st, "      sim> SET DZ VECT=octal   default 340\n");
fprintf (st, "\n The interrupt level can be set with: \n");
fprintf (st, "      sim> SET DZ BR=#     # should be between 4 and 7.\n");
fprintf (st, "\n The unibus addaptor that the DZ is on can be set with:\n");
fprintf (st, "      sim> SET DZ CTL=#    # can be either 1 or 3\n");
fprint_reg_help (st, &dz_dev);
fprintf (st, "\nThe additional terminals do not support save and restore.  All open connections\n");
fprintf (st, "are lost when the simulator shuts down or DZ is detached.\n");
return SCPE_OK;
}

const char *dz_description (DEVICE *dptr)
{
return "DZ11 asynchronous line interface";
}

#endif
