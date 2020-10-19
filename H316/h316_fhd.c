/* h316_fhd.c: H316/516 fixed head simulator

   Copyright (c) 2003-2015, Robert M. Supnik

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

   fhd          516-4400 fixed head disk

   03-Sep-13    RMS     Added explicit void * cast
   03-Jul-13    RLA     compatibility changes for extended interrupts
   19-Mar-12    RMS     Fixed declaration of chan_req (Mark Pizzolato)
   15-May-06    RMS     Fixed bug in autosize attach (David Gesswein)
   04-Jan-04    RMS     Changed sim_fsize calling sequence

   These head-per-track devices are buffered in memory, to minimize overhead.
*/

#include "h316_defs.h"
#include <math.h>

/* Constants */

#define FH_NUMWD        1536                            /* words/track */
#define FH_NUMTK        64                              /* tracks/surface */
#define FH_WDPSF        (FH_NUMWD * FH_NUMTK)           /* words/surface */             
#define FH_NUMSF        16                              /* surfaces/ctlr */
#define UNIT_V_AUTO     (UNIT_V_UF + 0)                 /* autosize */
#define UNIT_V_SF       (UNIT_V_UF + 1)                 /* #surfaces - 1 */
#define UNIT_M_SF       017
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_SF         (UNIT_M_SF << UNIT_V_SF)
#define UNIT_GETSF(x)   ((((x) >> UNIT_V_SF) & UNIT_M_SF) + 1)

/* Command word 1 */

#define CW1_RW          0100000                         /* read vs write */
#define CW1_V_SF        10                              /* surface */
#define CW1_M_SF        017
#define CW1_GETSF(x)    (((x) >> CW1_V_SF) & CW1_M_SF)
#define CW1_V_TK        4                               /* track */
#define CW1_M_TK        077
#define CW1_GETTK(x)    (((x) >> CW1_V_TK) & CW1_M_TK)

/* Command word 2 */

#define CW2_V_CA        0                               /* character addr */
#define CW2_M_CA        07777
#define CW2_GETCA(x)    (((x) >> CW2_V_CA) & CW2_M_CA)

#define GET_POS(x)      ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) FH_NUMWD)))

/* OTA states */

#define OTA_NOP         0                               /* normal */
#define OTA_CW1         1                               /* expecting CW1 */
#define OTA_CW2         2                               /* expecting CW2 */

extern int32 dev_int, dev_enb;
extern uint32 chan_req;
extern int32 stop_inst;
extern uint32 dma_ad[DMA_MAX];

uint32 fhd_cw1 = 0;                                     /* cmd word 1 */
uint32 fhd_cw2 = 0;                                     /* cmd word 2 */
uint32 fhd_buf = 0;                                     /* buffer */
uint32 fhd_otas = 0;                                    /* state */
uint32 fhd_busy = 0;                                    /* busy */
uint32 fhd_rdy = 0;                                     /* word ready */
uint32 fhd_dte = 0;                                     /* data err */
uint32 fhd_ace = 0;                                     /* access error */
uint32 fhd_dma = 0;                                     /* DMA/DMC */
uint32 fhd_eor = 0;                                     /* end of range */
uint32 fhd_csum = 0;                                    /* parity checksum */
uint32 fhd_stopioe = 1;                                 /* stop on error */
int32 fhd_time = 10;                                    /* time per word */

int32 fhdio (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat fhd_svc (UNIT *uptr);
t_stat fhd_reset (DEVICE *dptr);
t_stat fhd_attach (UNIT *uptr, CONST char *cptr);
t_stat fhd_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
void fhd_go (uint32 dma);
void fhd_go1 (uint32 dat);
void fhd_go2 (uint32 dat);
t_bool fhd_getc (UNIT *uptr, uint32 *ch);
t_bool fhd_putc (UNIT *uptr, uint32 ch);
t_bool fhd_bad_wa (uint32 wa);
uint32 fhd_csword (uint32 cs, uint32 ch);

/* FHD data structures

   fhd_dev      device descriptor
   fhd_unit     unit descriptor
   fhd_mod      unit modifiers
   fhd_reg      register list
*/

DIB fhd_dib = { FHD, 1, IOBUS, IOBUS, INT_V_FHD, INT_V_NONE, &fhdio, 0 };

UNIT fhd_unit = { 
    UDATA (&fhd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
           FH_WDPSF)
    };

REG fhd_reg[] = {
    { ORDATA (CW1, fhd_cw1, 16) },
    { ORDATA (CW2, fhd_cw2, 16) },
    { ORDATA (BUF, fhd_buf, 16) },
    { FLDATA (BUSY, fhd_busy, 0) },
    { FLDATA (RDY, fhd_rdy, 0) },
    { FLDATA (DTE, fhd_dte, 0) },
    { FLDATA (ACE, fhd_ace, 0) },
    { FLDATA (EOR, fhd_eor, 0) },
    { FLDATA (DMA, fhd_dma, 0) },
    { FLDATA (CSUM, fhd_csum, 7) },
    { FLDATA (INTREQ, dev_int, INT_V_MT) },
    { FLDATA (ENABLE, dev_enb, INT_V_MT) },
    { DRDATA (TIME, fhd_time, 31), REG_NZ + PV_LEFT },
    { ORDATA (OTAS, fhd_otas, 2), REG_HRO },
    { ORDATA (CHAN, fhd_dib.chan, 5), REG_HRO },
    { FLDATA (STOP_IOE, fhd_stopioe, 0) },
    { NULL }
    };

MTAB fhd_mod[] = {
    { UNIT_SF, (0 << UNIT_V_SF), NULL, "1S", &fhd_set_size },
    { UNIT_SF, (1 << UNIT_V_SF), NULL, "2S", &fhd_set_size },
    { UNIT_SF, (2 << UNIT_V_SF), NULL, "3S", &fhd_set_size },
    { UNIT_SF, (3 << UNIT_V_SF), NULL, "4S", &fhd_set_size },
    { UNIT_SF, (4 << UNIT_V_SF), NULL, "5S", &fhd_set_size },
    { UNIT_SF, (5 << UNIT_V_SF), NULL, "6S", &fhd_set_size },
    { UNIT_SF, (6 << UNIT_V_SF), NULL, "7S", &fhd_set_size },
    { UNIT_SF, (7 << UNIT_V_SF), NULL, "8S", &fhd_set_size },
    { UNIT_SF, (8 << UNIT_V_SF), NULL, "9S", &fhd_set_size },
    { UNIT_SF, (9 << UNIT_V_SF), NULL, "10S", &fhd_set_size },
    { UNIT_SF, (10 << UNIT_V_SF), NULL, "11S", &fhd_set_size },
    { UNIT_SF, (11 << UNIT_V_SF), NULL, "12S", &fhd_set_size },
    { UNIT_SF, (12 << UNIT_V_SF), NULL, "13S", &fhd_set_size },
    { UNIT_SF, (13 << UNIT_V_SF), NULL, "14S", &fhd_set_size },
    { UNIT_SF, (14 << UNIT_V_SF), NULL, "15S", &fhd_set_size },
    { UNIT_SF, (15 << UNIT_V_SF), NULL, "16S", &fhd_set_size },
    { UNIT_AUTO, UNIT_AUTO, "autosize", "AUTOSIZE", NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "IOBUS",
      &io_set_iobus, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DMC",
      &io_set_dmc, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DMA",
      &io_set_dma, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", NULL,
      NULL, &io_show_chan, NULL },
    { 0 }
    };

DEVICE fhd_dev = {
    "FHD", &fhd_unit, fhd_reg, fhd_mod,
    1, 8, 22, 1, 8, 16,
    NULL, NULL, &fhd_reset,
    NULL, &fhd_attach, NULL,
    &fhd_dib, DEV_DISABLE
    };

/* IO routines */

int32 fhdio (int32 inst, int32 fnc, int32 dat, int32 dev)
{
switch (inst) {                                         /* case on opcode */

    case ioOCP:                                         /* control */
        if (fnc == 04) {                                /* terminate output? */
            fhd_eor = 1;                                /* stop */
            CLR_INT (INT_FHD);                          /* clear int req */
            }
        else if (fnc == 003)                            /* start, DMA */
            fhd_go (1);
        else if (fnc == 007)                            /* start, IO bus */
            fhd_go (0);
        else return IOBADFNC (dat);
        break;

    case ioOTA:                                         /* output */
        if (fnc)                                        /* only fnc 0 */
            return IOBADFNC (dat);
        if (fhd_rdy) {                                  /* ready? */
            fhd_buf = dat;                              /* store data */
            if (fhd_otas == OTA_CW1)                    /* expecting CW1? */
                fhd_go1 (dat);
            else if (fhd_otas == OTA_CW2)               /* expecting CW2? */
                fhd_go2 (dat);
            else fhd_rdy = 0;                           /* normal, clr ready */
            return IOSKIP (dat);
            }
        break;

    case ioINA:                                         /* input */
        if (fnc)                                        /* only fnc 0 */
            return IOBADFNC (dat);
        if (fhd_rdy) {                                  /* ready? */
            fhd_rdy = 0;                                /* clear ready */
            return IOSKIP (dat | fhd_buf);              /* return data */
            }
        break;

    case ioSKS:                                         /* sense */
        if (((fnc == 000) && fhd_rdy) ||                /* 0 = skip if ready */
            ((fnc == 001) && !fhd_busy) ||              /* 1 = skip if !busy */
            ((fnc == 002) && !fhd_dte) ||               /* 2 = skip if !data err */
            ((fnc == 003) && !fhd_ace) ||               /* 3 = skip if !access err */
            ((fnc == 004) && !TST_INTREQ (INT_FHD)))    /* 4 = skip if !interrupt */
            return IOSKIP (dat);
        break;

    case ioEND:
        fhd_eor = 1;
        break;
        }

return dat;
}

/* Start new operation */

void fhd_go (uint32 dma)
{
int32 ch = fhd_dib.chan - 1;                            /* DMA/DMC chan */

if (fhd_busy)                                           /* ignore if busy */
    return;
fhd_busy = 1;                                           /* ctlr is busy */
fhd_eor = 0;                                            /* transfer not done */
fhd_csum = 0;                                           /* init checksum */
fhd_dte = 0;                                            /* clear errors */
fhd_ace = 0;
if (ch >= 0)                                            /* DMA allowed? */
    fhd_dma = dma;
else fhd_dma = 0;                                       /* no, force IO bus */
fhd_otas = OTA_CW1;                                     /* expect CW1 */
fhd_rdy = 1;                                            /* set ready */
if (fhd_dma && Q_DMA (ch)) {                            /* DMA and DMA channel? */
    SET_CH_REQ (ch);                                    /* set channel request */
    dma_ad[ch] = dma_ad[ch] & ~DMA_IN;                  /* force output */
    }
return;
}

/* Process command word 1 */

void fhd_go1 (uint32 dat)
{
int32 ch = fhd_dib.chan - 1;                            /* DMA/DMC chan */

fhd_cw1 = dat;                                          /* store CW1 */
fhd_otas = OTA_CW2;                                     /* expect CW2 */
fhd_rdy = 1;                                            /* set ready */
if (fhd_dma && Q_DMA (ch))                              /* DMA? set chan request */
    SET_CH_REQ (ch);
return;
}

/* Process command word 2 - initiate seek */

void fhd_go2 (uint32 dat)
{
int32 ch = fhd_dib.chan - 1;                            /* DMA/DMC chan */
uint32 sf = CW1_GETSF (fhd_cw1);                        /* surface */
int32 t, wa;

fhd_cw2 = dat;                                          /* store CW2 */
fhd_otas = OTA_NOP;                                     /* next state */
wa = CW2_GETCA (fhd_cw2) >> 1;                          /* word addr */
if ((wa >= FH_NUMWD) ||                                 /* if bad char addr */
    ((fhd_unit.flags & UNIT_ATT) == 0) ||               /* or unattached */
    (sf >= UNIT_GETSF (fhd_unit.flags))) {              /* or bad surface */
    fhd_ace = 1;                                        /* access error */
    fhd_busy = 0;                                       /* abort operation */
    SET_INT (INT_FHD);
    return;
    }
if (fhd_cw1 & CW1_RW) {                                 /* write? */
    fhd_rdy = 1;                                        /* set ready */
    if (fhd_dma)                                        /* if DMA/DMC, req chan */
        SET_CH_REQ (ch);
    }
else {
    fhd_rdy = 0;                                        /* read, clear ready */
    if (fhd_dma && (ch < DMC_V_DMC1))                   /* read and DMA chan? */
        dma_ad[ch] = dma_ad[ch] | DMA_IN;               /* force input */
    }
t = wa - GET_POS (fhd_time);                            /* delta to new loc */
if (t < 0)                                              /* wrap around? */
    t = t + FH_NUMWD;
sim_activate (&fhd_unit, t * fhd_time);                 /* schedule op */
return;
}

/* Unit service */

t_stat fhd_svc (UNIT *uptr)
{
int32 ch = fhd_dib.chan - 1;                            /* DMA/DMC chan (-1 if IO bus) */
uint32 c1, c2;

if ((uptr->flags & UNIT_ATT) == 0) {                    /* unattached? */
    fhd_ace = 1;                                        /* access error */
    fhd_busy = 0;                                       /* abort operation */
    SET_INT (INT_FHD);
    return IORETURN (fhd_stopioe, SCPE_UNATT);
    }

if (fhd_eor || fhd_rdy) {                               /* done or ready set? */
    if (fhd_rdy)                                        /* if ready set, data err */
        fhd_dte = 1;
    if (fhd_cw1 & CW1_RW) {                             /* write? */
        if (!fhd_rdy) {                                 /* buffer full? */
            fhd_putc (uptr, fhd_buf >> 8);              /* store last word */
            fhd_putc (uptr, fhd_buf);
            }
        fhd_putc (uptr, fhd_csum);                      /* store csum */
        }
    else {                                              /* read */
        fhd_getc (uptr, &c1);                           /* get csum */
        if (fhd_csum)                                   /* if csum != 0, err */
            fhd_dte = 1;
        }
    fhd_busy = 0;                                       /* operation complete */
    SET_INT (INT_FHD);
    return SCPE_OK;
    }

if (fhd_cw1 & CW1_RW) {                                 /* write? */
    if (fhd_putc (uptr, fhd_buf >> 8))
        return SCPE_OK;
    if (fhd_putc (uptr, fhd_buf))
        return SCPE_OK;
    }
else {                                                  /* read */
    if (fhd_getc (uptr, &c1))
        return SCPE_OK;
    if (fhd_getc (uptr, &c2))
        return SCPE_OK;
    fhd_buf = (c1 << 8) | c2;
    }
sim_activate (uptr, fhd_time);                          /* next word */
fhd_rdy = 1;                                            /* set ready */
if (fhd_dma)                                            /* if DMA/DMC, req chan */
    SET_CH_REQ (ch);
return SCPE_OK;
}

/* Read character from disk */

t_bool fhd_getc (UNIT *uptr, uint32 *ch)
{
uint32 sf = CW1_GETSF (fhd_cw1);                        /* surface */
uint32 tk = CW1_GETTK (fhd_cw1);                        /* track */
uint32 ca = CW2_GETCA (fhd_cw2);                        /* char addr */
uint32 wa = ca >> 1;                                    /* word addr */
uint32 ba = (((sf * FH_NUMTK) + tk) * FH_NUMWD) + wa;   /* buffer offset */
uint16 *fbuf = (uint16 *) uptr->filebuf;                /* buffer base */
uint32 wd;

if (fhd_bad_wa (wa))                                    /* addr bad? */
    return TRUE;
fhd_cw2 = fhd_cw2 + 1;                                  /* incr char addr */
if (ca & 1)                                             /* select char */
    wd = fbuf[ba] & 0377;
else wd = (fbuf[ba] >> 8) & 0377;
fhd_csum = fhd_csword (fhd_csum, wd);                   /* put in csum */
*ch = wd;                                               /* return */
return FALSE;
}

/* Write character to disk */

t_bool fhd_putc (UNIT *uptr, uint32 ch)
{
uint32 sf = CW1_GETSF (fhd_cw1);                        /* surface */
uint32 tk = CW1_GETTK (fhd_cw1);                        /* track */
uint32 ca = CW2_GETCA (fhd_cw2);                        /* char addr */
uint32 wa = ca >> 1;                                    /* word addr */
uint32 ba = (((sf * FH_NUMTK) + tk) * FH_NUMWD) + wa;   /* buffer offset */
uint16 *fbuf = (uint16 *)uptr->filebuf;                 /* buffer base */

ch = ch & 0377;                                         /* mask char */
if (fhd_bad_wa (wa))                                    /* addr bad? */
    return TRUE;
fhd_cw2 = fhd_cw2 + 1;                                  /* incr char addr */
if (ca & 1)                                             /* odd? low char */
    fbuf[ba] = (fbuf[ba] & ~0377) | ch;
else fbuf[ba] = (fbuf[ba] & 0377) | (ch << 8);          /* even, hi char */
fhd_csum = fhd_csword (fhd_csum, ch);                   /* put in csum */
if (ba >= uptr->hwmark)                                 /* update hwmark */
    uptr->hwmark = ba + 1;
return FALSE;
}

/* Check word address */

t_bool fhd_bad_wa (uint32 wa)
{
if (wa >= FH_NUMWD) {                                   /* bad address? */
    fhd_ace = 1;                                        /* access error */
    fhd_busy = 0;                                       /* abort operation */
    SET_INT (INT_FHD);
    return TRUE;
    }
return FALSE;
}

/* Add character to checksum (parity) */

uint32 fhd_csword (uint32 cs, uint32 ch)
{
while (ch) {                                            /* count bits */
    ch = ch & ~(ch & (-(int32) ch));
    cs = cs ^ 0200;                                     /* invert cs for each 1 */
    }
return cs;
}

/* Reset routine */

t_stat fhd_reset (DEVICE *dptr)
{
fhd_busy = 0;                                           /* reset state */
fhd_rdy = 0;
fhd_ace = 0;
fhd_dte = 0;
fhd_eor = 0;
fhd_otas = OTA_NOP;
fhd_cw1 = fhd_cw2 = fhd_buf = 0;
CLR_INT (INT_FHD);                                      /* clear int, enb */
CLR_ENB (INT_FHD);
sim_cancel (&fhd_unit);                                 /* cancel operation */
return SCPE_OK;
}

/* Attach routine */

t_stat fhd_attach (UNIT *uptr, CONST char *cptr)
{
uint32 sz, sf;
uint32 ds_bytes = FH_WDPSF * sizeof (int16);

if ((uptr->flags & UNIT_AUTO) && (sz = sim_fsize_name (cptr))) {
    sf = (sz + ds_bytes - 1) / ds_bytes;
    if (sf >= FH_NUMSF)
        sf = FH_NUMSF - 1;
    uptr->flags = (uptr->flags & ~UNIT_SF) |
        (sf << UNIT_V_SF);
    }
uptr->capac = UNIT_GETSF (uptr->flags) * FH_WDPSF;
return attach_unit (uptr, cptr);
}

/* Set size routine */

t_stat fhd_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (val < 0)
    return SCPE_IERR;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = UNIT_GETSF (val) * FH_WDPSF;
return SCPE_OK;
}
