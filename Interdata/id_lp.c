/* id_lp.c: Interdata line printer

   Copyright (c) 2001-2008, Robert M. Supnik

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

   lpt          M46-206 line printer

   27-May-08    RMS     Fixed bug in printing test (Davis Johnson)
   19-Jan-07    RMS     Added UNIT_TEXT flag
   25-Apr-03    RMS     Revised for extended file support
*/

#include "id_defs.h"
#include <ctype.h>

/* Device definitions */

#define UNIT_V_UC       (UNIT_V_UF + 0)                 /* UC only */
#define UNIT_UC         (1 << UNIT_V_UC)
#define SPC_BASE        0x40                            /* spacing base */
#define VFU_BASE        0x78                            /* VFU base */
#define VFU_WIDTH       0x8                             /* VFU width */
#define LF              0xA
#define VT              0xB
#define VT_VFU          4                               /* VFU chan for VT */
#define FF              0xC
#define FF_VFU          8                               /* VFU chan for FF */
#define CR              0xD
#define VFUP(ch,val)    ((val) & (1 << (ch)))           /* VFU chan test */

/* Status byte, * = dynamic */

#define STA_PAPE        0x40                            /* *paper empty */
#define STA_MASK        (STA_BSY)                       /* static status */

uint32 lpt_sta = STA_BSY;                               /* status */
char lpxb[LPT_WIDTH + 1];                               /* line buffer */
uint32 lpt_bptr = 0;                                    /* buf ptr */
uint32 lpt_spnd = 0;                                    /* space pending */
uint32 lpt_vfup = 0;                                    /* VFU ptr */
uint32 lpt_vful = 1;                                    /* VFU lnt */
uint8 lpt_vfut[VFU_LNT] = { 0xFF };                     /* VFU tape */
uint32 lpt_arm = 0;                                     /* int armed */
int32 lpt_ctime = 10;                                   /* char time */
int32 lpt_stime = 1000;                                 /* space time */
int32 lpt_stopioe = 0;                                  /* stop on err */

extern uint32 int_req[INTSZ], int_enb[INTSZ];

uint32 lpt (uint32 dev, uint32 op, uint32 dat);
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_attach (UNIT *uptr, CONST char *cptr);
t_stat lpt_bufout (UNIT *uptr);
t_stat lpt_vfu (UNIT *uptr, int32 ch);
t_stat lpt_spc (UNIT *uptr, int32 cnt);

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptors
   lpt_reg      LPT register list
*/

DIB lpt_dib = { d_LPT, -1, v_LPT, NULL, &lpt, NULL };

UNIT lpt_unit = { UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_UC+UNIT_TEXT, 0) };

REG lpt_reg[] = {
    { HRDATA (STA, lpt_sta, 8) },
    { HRDATA (BUF, lpt_unit.buf, 7) },
    { BRDATA (DBUF, lpxb, 16, 7, sizeof (lpxb)) },
    { HRDATA (DBPTR, lpt_bptr, 8) },
    { HRDATA (VFUP, lpt_vfup, 8) },
    { HRDATA (VFUL, lpt_vful, 8) },
    { BRDATA (VFUT, lpt_vfut, 16, 8, VFU_LNT) },
    { FLDATA (IREQ, int_req[l_LPT], i_LPT) },
    { FLDATA (IENB, int_enb[l_LPT], i_LPT) },
    { FLDATA (IARM, lpt_arm, 0) },
    { DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (CTIME, lpt_ctime, 24), PV_LEFT },
    { DRDATA (STIME, lpt_stime, 24), PV_LEFT },
    { FLDATA (STOP_IOE, lpt_stopioe, 0) },
    { HRDATA (DEVNO, lpt_dib.dno, 8), REG_HRO },
    { NULL }
    };

MTAB lpt_mod[] = {
    { UNIT_UC, 0, "lower case", "LC", NULL },
    { UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 16, 7,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, NULL,
    &lpt_dib, DEV_DISABLE
    };

/* Line printer: IO routine */

uint32 lpt (uint32 dev, uint32 op, uint32 dat)
{
int32 t;

switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        return BY;                                      /* byte only */

    case IO_OC:                                         /* command */
        lpt_arm = int_chg (v_LPT, dat, lpt_arm);        /* upd int ctrl */
        break;

    case IO_WD:                                         /* write */
        t = lpt_unit.buf = dat & 0x7F;                  /* mask char */
        lpt_sta = STA_BSY;                              /* set busy */
        if (lpt_spnd || ((t >= LF) && (t <= CR)))       /* space op? */
            sim_activate (&lpt_unit, lpt_stime);
        else sim_activate (&lpt_unit, lpt_ctime);       /* normal char */
        break;

    case IO_SS:                                         /* status */
        t = lpt_sta & STA_MASK;                         /* status byte */
        if ((lpt_unit.flags & UNIT_ATT) == 0)           /* test paper out */
            t = t | STA_EX | STA_PAPE | STA_BSY;
        return t;
        }

return 0;
}

/* Unit service */

t_stat lpt_svc (UNIT *uptr)
{
int32 t;
t_stat r = SCPE_OK;

lpt_sta = 0;                                            /* clear busy */
if (lpt_arm)                                            /* armed? intr */
    SET_INT (v_LPT);
if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return IORETURN (lpt_stopioe, SCPE_UNATT);
t = uptr->buf;                                          /* get character */
if (lpt_spnd || ((t >= LF) && (t < CR))) {              /* spc pend or spc op? */
    lpt_spnd = 0;
    if (lpt_bufout (uptr) != SCPE_OK)                   /* print */
        return SCPE_IOERR;
    if ((t == 1) || (t == LF))                          /* single space */
        lpt_spc (uptr, 1);
    else if (t == VT)                                   /* VT->VFU */
        r = lpt_vfu (uptr, VT_VFU - 1);
    else if (t == 0xC)                                  /* FF->VFU */
        r = lpt_vfu (uptr, FF_VFU - 1);
    else if ((t >= SPC_BASE) && (t < VFU_BASE))
        lpt_spc (uptr, t - SPC_BASE);                   /* space */
    else if ((t >= VFU_BASE) && (t < VFU_BASE + VFU_WIDTH))
        r = lpt_vfu (uptr, t - VFU_BASE);               /* VFU */
    else fputs ("\r", uptr->fileref);                   /* overprint */
    uptr->pos = ftell (uptr->fileref);                  /* update position */
    if (ferror (lpt_unit.fileref)) {
        sim_perror ("LPT I/O error");
        clearerr (uptr->fileref);
        return SCPE_IOERR;
        }
    }
else if (t == CR) {                                     /* CR? */
    lpt_spnd = 1;                                       /* set spc pend */
    return lpt_bufout (uptr);                           /* print line */
    }
else if (t >= 0x20) {                                   /* printable? */
    if ((uptr->flags & UNIT_UC) && islower (t))         /* UC only? */
        t = toupper (t);
    if (lpt_bptr < LPT_WIDTH)
        lpxb[lpt_bptr++] = t;
    }
return r;
}

/* Printing and spacing routines */

t_stat lpt_bufout (UNIT *uptr)
{
int32 i;
t_stat r = SCPE_OK;

if (lpt_bptr == 0) return SCPE_OK;                      /* any char in buf? */
for (i = LPT_WIDTH - 1; (i >= 0) && (lpxb[i] == ' '); i--)
    lpxb[i] = 0;                                        /* backscan line */
if (lpxb[0]) {                                          /* any char left? */
    fputs (lpxb, uptr->fileref);                        /* write line */
    lpt_unit.pos = ftell (uptr->fileref);               /* update position */
    if (ferror (uptr->fileref)) {
        sim_perror ("LPT I/O error");
        clearerr (uptr->fileref);
        r = SCPE_IOERR;
        }
    }   
lpt_bptr = 0;                                           /* reset buffer */
for (i = 0; i < LPT_WIDTH; i++)
    lpxb[i] = ' ';
lpxb[LPT_WIDTH] = 0;
return r;
}

t_stat lpt_vfu (UNIT *uptr, int32 ch)
{
uint32 i, j;

if ((ch == (FF_VFU - 1)) && VFUP (ch, lpt_vfut[0])) {   /* top of form? */
    fputs ("\n\f", uptr->fileref);                      /* nl + ff */
    lpt_vfup = 0;                                       /* top of page */
    return SCPE_OK;
    }
for (i = 1; i < lpt_vful + 1; i++) {                    /* sweep thru cct */
    lpt_vfup = (lpt_vfup + 1) % lpt_vful;               /* adv pointer */
    if (VFUP (ch, lpt_vfut[lpt_vfup])) {                /* chan punched? */
        for (j = 0; j < i; j++)
            fputc ('\n', uptr->fileref);
        return SCPE_OK;
        }
    }
return STOP_VFU;                                        /* runaway channel */
}

t_stat lpt_spc (UNIT *uptr, int32 cnt)
{
int32 i;

if (cnt == 0)
     fputc ('\r', uptr->fileref);
else {
    for (i = 0; i < cnt; i++)
        fputc ('\n', uptr->fileref);
    lpt_vfup = (lpt_vfup + cnt) % lpt_vful;
    }
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
int32 i;

sim_cancel (&lpt_unit);                                 /* deactivate */
lpt_sta = 0;                                            /* clr busy */
lpt_bptr = 0;                                           /* clr buf ptr */
for (i = 0; i < LPT_WIDTH; i++)                         /* clr buf */
    lpxb[i] = ' ';
lpxb[LPT_WIDTH] = 0;
CLR_INT (v_LPT);                                        /* clearr int */
CLR_ENB (v_LPT);                                        /* disable int */
lpt_arm = 0;                                            /* disarm int */
return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
lpt_vfup = 0;                                           /* top of form */
sim_switches |= SWMASK ('A');                           /* position to EOF */
return attach_unit (uptr, cptr);
}

/* Carriage control load routine */

t_stat lp_load (FILE *fileref, CONST char *cptr, CONST char *fnam)
{
int32 col, ptr, mask, vfubuf[VFU_LNT];
uint32 rpt;
t_stat r;
char cbuf[CBUFSIZE], gbuf[CBUFSIZE];

if (*cptr != 0)
    return SCPE_ARG;
ptr = 0;
for ( ; (cptr = fgets (cbuf, CBUFSIZE, fileref)) != NULL; ) { /* until eof */
    mask = 0;
    if (*cptr == '(') {                                 /* repeat count? */
        cptr = get_glyph (cptr + 1, gbuf, ')');         /* get 1st field */
        rpt = get_uint (gbuf, 10, VFU_LNT, &r);         /* repeat count */
        if (r != SCPE_OK)
            return SCPE_FMT;
        }
    else rpt = 1;
    while (*cptr != 0) {                                /* get col no's */
        cptr = get_glyph (cptr, gbuf, ',');             /* get next field */
        col = get_uint (gbuf, 10, 7, &r);               /* column number */
        if (r != SCPE_OK)
            return SCPE_FMT;
        mask = mask | (1 << col);                       /* set bit */
        }
    for ( ; rpt > 0; rpt--) {                           /* store vals */
        if (ptr >= VFU_LNT)
            return SCPE_FMT;
        vfubuf[ptr++] = mask;
        }
    }
if (ptr == 0)
    return SCPE_FMT;
lpt_vful = ptr;
lpt_vfup = 0;
for (rpt = 0; rpt < lpt_vful; rpt++)
    lpt_vfut[rpt] = vfubuf[rpt];
return SCPE_OK;
}
