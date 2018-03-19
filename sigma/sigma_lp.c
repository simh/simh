/* sigma_lp.c: Sigma 7440/7450 line printer

   Copyright (c) 2007-2017, Robert M. Supnik

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

   lp           7440/7445 or 7450 line printer

   09-Mar-2017  RMS     Fixed unclosed file returns in CCT load (COVERITY)
*/

#include "sigma_io_defs.h"

/* Device definitions */

#define CCT_LNT         256                             /* carriage ctl max */
#define BUF_LNT4        132                             /* line lengths */
#define BUF_LNT5        128

#define LP_7440         0                               /* models */
#define LP_7450         1

/* Device states */

#define LPS_INIT        0x101
#define LPS_END         0x102
#define LPS_PRI         0x1
#define LPS_FMT         0x3
#define LPS_FMTP        0x5
#define LPS_INT         0x40

/* Device status */

#define LPDV_ODD        0x40                            /* odd */
#define LPDV_TOF        0x10                            /* top of form */
#define LPDV_MOV        0x08                            /* paper moving */
#define LPDV_V_RUN      2                               /* runaway CCT */
#define LPDV_RUN        (1u << LPDV_V_RUN)
#define LPDV_WT2        0x02                            /* waiting for 2nd */

/* Format characters */

#define FMT_INH         0x60
#define FMT_SPC         0xC0
#define FMT_SKP         0xF0

#define FMT_MSPC4       15                              /* max space cmd */
#define FMT_MSPC5       7
#define SPC_MASK        ((lp_model == LP_7440)? FMT_MSPC4: FMT_MSPC5)
#define FMT_MCH4        7                               /* max CCT channel */
#define FMT_MCH5        1
#define CCH_MASK        ((lp_model == LP_7440)? FMT_MCH5: FMT_MCH4)

#define CH_BOF          0                               /* CCT bot of form */
#define CH_TOF          1                               /* CCT top of form */

#define CHP(ch,val)     ((val) & (1 << (ch)))

uint32 lp_cmd = 0;
uint32 lp_stopioe = 1;
uint32 lp_cctp = 0;                                     /* CCT position */
uint32 lp_cctl = 1;                                     /* CCT length */
uint32 lp_lastcmd = 0;                                  /* last command */
uint32 lp_pass = 0;                                     /* 7450 print pass */
uint32 lp_inh = 0;                                      /* space inhibit */
uint32 lp_run = 0;                                      /* CCT runaway */
uint32 lp_model = LP_7440;
uint8 lp_buf[BUF_LNT4];                                 /* print buffer */
uint8 lp_cct[CCT_LNT] = { 0xFF };                       /* carriage ctl tape */
uint8 lp_to_ascii[64] = {
    ' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '`', '.', '<', '(', '+', '|',
    '&', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ')', ';', '~',
    '-', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '^', ',', '%', '_', '>', '?',
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', ':', '#', '@', '\'', '=', '"'
    };

extern uint32 chan_ctl_time;

uint32 lp_disp (uint32 op, uint32 dva, uint32 *dvst);
uint32 lp_tio_status (void);
uint32 lp_tdv_status (void);
t_stat lp_chan_err (uint32 st);
t_stat lp_svc (UNIT *uptr);
t_stat lp_reset (DEVICE *dptr);
t_stat lp_attach (UNIT *uptr, CONST char *cptr);
t_stat lp_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat lp_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat lp_load_cct (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat lp_read_cct (FILE *cfile);
uint32 lp_fmt (UNIT *uptr);
uint32 lp_skip (UNIT *uptr, uint32 ch);
uint32 lp_space (UNIT *uptr, uint32 lines, t_bool skp);
uint32 lp_print (UNIT *uptr);

/* LP data structures

   lp_dev       LP device descriptor
   lp_unit      LP unit descriptors
   lp_reg       LP register list
   lp_mod       LP modifiers list
*/

dib_t lp_dib = { DVA_LP, lp_disp, 0, NULL };

UNIT lp_unit = { UDATA (&lp_svc, UNIT_ATTABLE+UNIT_SEQ, 0), SERIAL_OUT_WAIT };

REG lp_reg[] = {
    { HRDATA (CMD, lp_cmd, 9) },
    { BRDATA (BUF, lp_buf, 16, 7, BUF_LNT4) },
    { FLDATA (PASS, lp_pass, 0) },
    { FLDATA (INH, lp_inh, 0) },
    { FLDATA (RUNAWAY, lp_run, LPDV_V_RUN) },
    { BRDATA (CCT, lp_cct, 8, 8, CCT_LNT) },
    { DRDATA (CCTP, lp_cctp, 8), PV_LEFT },
    { DRDATA (CCTL, lp_cctl, 8), PV_LEFT + REG_HRO + REG_NZ },
    { DRDATA (POS, lp_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, lp_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, lp_stopioe, 0) },
    { HRDATA (LASTC, lp_lastcmd, 8), REG_HIDDEN },
    { FLDATA (MODEL, lp_model, 0), REG_HRO },
    { HRDATA (DEVNO, lp_dib.dva, 12), REG_HRO },
    { NULL }
    };

MTAB lp_mod[] = {
    { MTAB_XTD | MTAB_VDV, LP_7440, NULL, "7440",
      &lp_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, LP_7450, NULL, "7450",
      &lp_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 0, "TYPE", NULL,
      NULL, &lp_showtype, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NC, 0, NULL, "CCT",
      &lp_load_cct, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "CHAN", "CHAN",
      &io_set_dvc, &io_show_dvc, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DVA", "DVA",
      &io_set_dva, &io_show_dva, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "CSTATE", NULL,
      NULL, &io_show_cst, NULL },
    { 0 }
    };

DEVICE lp_dev = {
    "LP", &lp_unit, lp_reg, lp_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &lp_reset,
    NULL, &lp_attach, NULL,
    &lp_dib, 0
    };

/* Line printer: IO dispatch routine */

uint32 lp_disp (uint32 op, uint32 dva, uint32 *dvst)
{
switch (op) {                                           /* case on op */

    case OP_SIO:                                        /* start I/O */
        *dvst = lp_tio_status ();                       /* get status */
        if ((*dvst & DVS_DST) == 0) {                   /* idle? */
            lp_cmd = LPS_INIT;                          /* start dev thread */
            sim_activate (&lp_unit, chan_ctl_time);
            }
        break;

    case OP_TIO:                                        /* test status */
        *dvst = lp_tio_status ();                       /* return status */
        break;

    case OP_TDV:                                        /* test status */
        *dvst = lp_tdv_status ();                       /* return status */
        break;

    case OP_HIO:                                        /* halt I/O */
        chan_clr_chi (lp_dib.dva);                      /* clear int */
        *dvst = lp_tio_status ();                       /* get status */
        if ((*dvst & DVS_DST) != 0) {                   /* busy? */
            sim_cancel (&lp_unit);                      /* stop dev thread */
            chan_uen (lp_dib.dva);                      /* uend */
            }
        break;

    case OP_AIO:                                        /* acknowledge int */
        chan_clr_chi (lp_dib.dva);                      /* clear int */
        *dvst = lp_lastcmd & LPS_INT;                   /* int requested */
        lp_lastcmd = 0;
        break;

    default:
        *dvst = 0;
        return SCPE_IERR;
        }

return 0;
}

/* Service routine */

t_stat lp_svc (UNIT *uptr)
{
uint32 cmd;
uint32 st;

switch (lp_cmd) {                                       /* case on state */

    case LPS_INIT:                                      /* I/O init */
        st = chan_get_cmd (lp_dib.dva, &cmd);           /* get command */
        if (CHS_IFERR (st))                             /* channel error? */
            return lp_chan_err (st);
        lp_inh = 0;                                     /* clear inhibit, */
        lp_run = 0;                                     /* runaway */
        lp_cmd = lp_lastcmd = cmd;                      /* save command */
        sim_activate (uptr, chan_ctl_time);             /* continue thread */
        break;

    case LPS_FMT:
    case LPS_FMT|LPS_INT:                               /* format only */
        sim_activate (uptr, uptr->wait);                /* continue thread */
        if ((uptr->flags & UNIT_ATT) == 0)              /* not attached? */
            return lp_stopioe? SCPE_UNATT: SCPE_OK;
        st = lp_fmt (uptr);                             /* format */
        if (CHS_IFERR (st))                             /* error? */
            return lp_chan_err (st);
        if ((lp_model == LP_7440) &&                    /* 7440? lnt chk */
            (st != CHS_ZBC) &&
            chan_set_chf (lp_dib.dva, CHF_LNTE))        /* not ignored? */
            return lp_chan_err (SCPE_OK);               /* force uend */
        lp_cmd = LPS_END;                               /* actual print */
        break;

    case LPS_FMTP:
    case LPS_FMTP|LPS_INT:                              /* format and print */
        sim_activate (uptr, uptr->wait);                /* continue thread */
        if ((uptr->flags & UNIT_ATT) == 0)              /* not attached? */
            return lp_stopioe? SCPE_UNATT: SCPE_OK;
        st = lp_fmt (uptr);                             /* format */
        if (CHS_IFERR (st))                             /* error? */
            return lp_chan_err (st);
        if (st == CHS_ZBC) {                            /* command done? */
            if ((lp_model == LP_7440) &&                /* 7440? lnt err */
                chan_set_chf (lp_dib.dva, CHF_LNTE))    /* not ignored? */
                return lp_chan_err (SCPE_OK);
            }
        else {                                          /* more to do */
            st = lp_print (uptr);                       /* print */
            if (CHS_IFERR (st))                         /* error */
                return lp_chan_err (st);
            }
        break;

    case LPS_PRI:
    case LPS_PRI|LPS_INT:                               /* print only */
        sim_activate (uptr, uptr->wait);                /* continue thread */
        if ((uptr->flags & UNIT_ATT) == 0)              /* not attached? */
            return lp_stopioe? SCPE_UNATT: SCPE_OK;
        st = lp_print (uptr);                           /* print */
        if (CHS_IFERR (st))                             /* error? */
            return lp_chan_err (st);
        break;

    case LPS_END:                                       /* command done */
        if ((lp_lastcmd & LPS_INT) && !lp_pass)         /* int requested? */
            chan_set_chi (lp_dib.dva, 0);
        st = chan_end (lp_dib.dva);                     /* set channel end */
        if (CHS_IFERR (st))                             /* channel error? */
            return lp_chan_err (st);
        if (st == CHS_CCH) {                            /* command chain? */
            lp_cmd = LPS_INIT;                          /* restart thread */
            sim_activate (uptr, chan_ctl_time);
            }
        break;

     default:                                           /* invalid cmd */
        chan_uen (lp_dib.dva);                          /* uend */
        break;
        }

return SCPE_OK;
}

/* Format routine */

uint32 lp_fmt (UNIT *uptr)
{
uint32 c, i;
uint32 st;

st = chan_RdMemB (lp_dib.dva, &c);                      /* get char */
if (CHS_IFERR (st))                                     /* channel error? */
    return st;                                          /* caller handles */
if (lp_pass)                                            /* only on pass 1 */
    return 0;
if ((c & 0x7F) == FMT_INH)                              /* inhibit? */
    lp_inh = 1;
else if ((c & ~(((lp_model == LP_7450)? 0x20: 0) | SPC_MASK)) == FMT_SPC) {
    c = c & SPC_MASK;                                   /* space? */
    for (i = 1; i <= c; i++) {                          /* look for BOF */
        if (CHP (CH_BOF, lp_cct[(lp_cctp + i) % lp_cctl]))
            return lp_skip (uptr, CH_TOF);              /* found, TOF */
        }        
    return lp_space (uptr, c, FALSE);                   /* space */
    }
else if ((c & ~CCH_MASK) == FMT_SKP)                    /* skip? */
    return lp_skip (uptr, c & CCH_MASK);                /* skip to chan */
return 0;
}

/* Skip to channel */

uint32 lp_skip (UNIT *uptr, uint32 ch)
{
uint32 i;

for (i = 1; i < (lp_cctl + 1); i++) {                   /* sweep thru CCT */
    if (CHP (ch, lp_cct[(lp_cctp + i) % lp_cctl]))      /* channel punched? */
        return lp_space (uptr, i, TRUE);                /* space to chan */
    }
lp_run = LPDV_RUN;                                      /* runaway CCT */
return lp_space (uptr, lp_cctl, TRUE);                  /* space max */
}

/* Space routine */

uint32 lp_space (UNIT *uptr, uint32 cnt, t_bool skp)
{
uint32 i;

lp_cctp = (lp_cctp + cnt) % lp_cctl;                    /* adv cct, mod lnt */
if (skp && CHP (CH_TOF, lp_cct[lp_cctp]))               /* skip, TOF? */
        fputs ("\f", uptr->fileref);                    /* ff */
    else {                                              /* space */
        for (i = 0; i < cnt; i++)
            fputc ('\n', uptr->fileref);
        }
uptr->pos = ftell (uptr->fileref);                      /* update position */
if (ferror (uptr->fileref)) {                           /* error? */
    sim_perror ("Line printer I/O error");
    clearerr (uptr->fileref);
    chan_set_chf (lp_dib.dva, CHF_XMDE);
    return SCPE_IOERR;
    }
return 0;
}

/* Print routine */

uint32 lp_print (UNIT *uptr)
{
uint32 i, bp, c;
uint32 max = (lp_model == LP_7440)? BUF_LNT4: BUF_LNT5;
uint32 st;

if (lp_pass == 0) {                                     /* pass 1? clr buf */
    for (i = 0; i < BUF_LNT4; i++) lp_buf[i] = ' ';
    }
for (bp = 0, st = 0; (bp < max) && !st; bp++) {          /* fill buffer */
    st = chan_RdMemB (lp_dib.dva, &c);                  /* get char */
    if (CHS_IFERR (st))                                 /* channel error? */
        return st;                                      /* caller handles */
    if ((lp_model == LP_7440) ||                        /* 7440 or */
        ((bp & 1) == lp_pass))                          /* correct pass? */
        lp_buf[bp] = lp_to_ascii[c & 0x3F];
    }
if ((lp_model == LP_7440) || lp_pass) {                 /* ready to print? */
    lp_pass = 0;
    for (i = BUF_LNT4; (i > 0) && (lp_buf[i - 1] == ' '); i--) ; /* trim */
    if (i)                                              /* write line */
        sim_fwrite (lp_buf, 1, i, uptr->fileref);
    fputc (lp_inh? '\r': '\n', uptr->fileref);          /* cr or nl */
    uptr->pos = ftell (uptr->fileref);                  /* update position */
    if (ferror (uptr->fileref)) {                       /* error? */
        sim_perror ("Line printer I/O error");
        clearerr (uptr->fileref);
        chan_set_chf (lp_dib.dva, CHF_XMDE);
        return SCPE_IOERR;
        }
    if ((lp_model == LP_7440) &&                        /* 7440? */
        ((bp != BUF_LNT4) || (st != CHS_ZBC)) &&        /* check lnt err */
        chan_set_chf (lp_dib.dva, CHF_LNTE))
        return CHS_INACTV;                              /* stop if asked */
    }
else lp_pass = 1;                                       /* 7450 pass 2 */
lp_cmd = LPS_END;                                       /* end state */
return 0;
}

/* LP status routine */

uint32 lp_tio_status (void)
{
uint32 st;

st = (lp_unit.flags & UNIT_ATT)? DVS_AUTO: 0;           /* auto? */
if (sim_is_active (&lp_unit))                           /* busy? */
    st |= (DVS_CBUSY | DVS_DBUSY | (CC2 << DVT_V_CC));
return st;
}

uint32 lp_tdv_status (void)
{
uint32 st;

st = lp_run;                                            /* runaway flag */
if ((lp_unit.flags & UNIT_ATT) == 0)                    /* fault? */
    st |= (CC2 << DVT_V_CC);
if (lp_cmd == LPS_END)                                  /* end state? */
    st |= LPDV_MOV;                                     /* printing */
if (lp_pass && (lp_model == LP_7450)) {                 /* 7450 pass 2? */
    st |= LPDV_ODD;                                     /* odd state */
    if (lp_cmd == LPS_INIT)                             /* wait for cmd? */
        st |= LPDV_WT2;
    }
return st;
}

/* Channel error */

t_stat lp_chan_err (uint32 st)
{
sim_cancel (&lp_unit);                                  /* stop dev thread */
chan_uen (lp_dib.dva);                                  /* uend */
if (st < CHS_ERR)
    return st;
return SCPE_OK;
}

/* Reset routine */

t_stat lp_reset (DEVICE *dptr)
{
sim_cancel (&lp_unit);                                 /* stop dev thread */
lp_cmd = 0;
lp_lastcmd = 0;
lp_pass = 0;
lp_inh = 0;
lp_run = 0;
chan_reset_dev (lp_dib.dva);                           /* clr int, active */
return SCPE_OK;
}

/* Attach routine */

t_stat lp_attach (UNIT *uptr, CONST char *cptr)
{
lp_cctp = 0;                                            /* clear cct ptr */
lp_pass = 0;
return attach_unit (uptr, cptr);
}

/* Set handler for carriage control tape */

t_stat lp_load_cct (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
FILE *cfile;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_ARG;
if ((cfile = fopen (cptr, "r")) == NULL)
    return SCPE_OPENERR;
r = lp_read_cct (cfile);
fclose (cfile);
return r;
}

/* Read carriage control tape - used by SET and LOAD */

t_stat lp_read_cct (FILE *cfile)
{
uint32 col, rpt, ptr, mask;
uint8 cctbuf[CCT_LNT];
CONST char *cptr;
t_stat r;
char cbuf[CBUFSIZE], gbuf[CBUFSIZE];

ptr = 0;
for ( ; (cptr = fgets (cbuf, CBUFSIZE, cfile)) != NULL; ) {
    mask = 0;
    if (*cptr == '(') {                                 /* repeat count? */
        cptr = get_glyph (cptr + 1, gbuf, ')');         /* get 1st field */
        rpt = get_uint (gbuf, 10, CCT_LNT, &r);         /* repeat count */
        if (r != SCPE_OK)
            return SCPE_FMT;
        }
    else rpt = 1;
    while (*cptr != 0) {                                /* get col no's */
        cptr = get_glyph (cptr, gbuf, ',');             /* get next field */
        col = get_uint (gbuf, 10, FMT_MCH4, &r);        /* column number */
        if (r != SCPE_OK)
            return SCPE_FMT;
        mask = mask | (1 << col);                       /* set bit */
        }
    for ( ; rpt > 0; rpt--) {                           /* store vals */
        if (ptr >= CCT_LNT)
            return SCPE_FMT;
        cctbuf[ptr++] = mask;
        }
    }
if (ptr == 0)
    return SCPE_FMT;
lp_cctl = ptr;
lp_cctp = 0;
for (rpt = 0; rpt < lp_cctl; rpt++)
    lp_cct[rpt] = cctbuf[rpt];
return SCPE_OK;
}

/* Set controller type */

t_stat lp_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
lp_model = val;
lp_reset (&lp_dev);
return SCPE_OK;
}

/* Show controller type */

t_stat lp_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, lp_model? "7450": "7440");
return SCPE_OK;
}
