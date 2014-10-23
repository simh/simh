/* id_io.c: Interdata CPU-independent I/O routines

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

   30-Mar-06    RMS     Fixed bug, GO preserves EXA and SSTA (Davis Johnson)
   21-Jun-03    RMS     Changed subroutine argument for ARM compiler conflict

   Interdata I/O devices are defined by a device information block:

        dno             base device number
        sch             selector channel, -1 if none
        irq             interrupt request flag
        tplte           device number template, NULL if one device number
        iot             I/O processing routine
        ini             initialization routine

   Interdata I/O uses the following interconnected tables:

        dev_tab[dev]    Indexed by device number, points to the I/O instruction
                        processing routine for the device.

        sch_tab[dev]    Indexed by device number, if non-zero, the number + 1
                        of the selector channel used by the device.

        int_req[level]  Indexed by interrupt level, device interrupt flags.

        int_enb[level]  Indexed by interrupt level, device interrupt enable flags.

        int_tab[idx]    Indexed by ((interrupt level * 32) + interrupt number),
                        maps bit positions in int_req to device numbers.
*/

#include "id_defs.h"

/* Selector channel */

#define SCHC_EXA        0x40                            /* read ext addr */
#define SCHC_RD         0x20                            /* read */
#define SCHC_GO         0x10                            /* go */
#define SCHC_STOP       0x08                            /* stop */
#define SCHC_SSTA       0x04                            /* sel ch status */
#define SCHC_EXM        0x03                            /* ext mem */

extern uint32 int_req[INTSZ], int_enb[INTSZ];
extern uint32 (*dev_tab[DEVNO])(uint32 dev, uint32 op, uint32 datout);
extern uint32 pawidth;
extern UNIT cpu_unit;

uint32 sch_max = 2;                                     /* sch count */
uint32 sch_sa[SCH_NUMCH] = { 0 };                       /* start addr */
uint32 sch_ea[SCH_NUMCH] = { 0 };                       /* end addr */
uint8 sch_sdv[SCH_NUMCH] = { 0 };                       /* device */
uint8 sch_cmd[SCH_NUMCH] = { 0 };                       /* command */
uint8 sch_rdp[SCH_NUMCH] = { 0 };                       /* read ptr */
uint8 sch_wdc[SCH_NUMCH] = { 0 };                       /* write ctr */
uint32 sch_tab[DEVNO] = { 0 };                          /* dev to sch map */
uint32 int_tab[INTSZ * 32] = { 0 };                     /* int to dev map */
uint8 sch_tplte[SCH_NUMCH + 1];                         /* dnum template */

uint32 sch (uint32 dev, uint32 op, uint32 dat);
void sch_ini (t_bool dtpl);
t_stat sch_reset (DEVICE *dptr);
t_stat sch_set_nchan (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat sch_show_nchan (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat sch_show_reg (FILE *st, UNIT *uptr, int32 val, void *desc);

/* Selector channel data structures

   sch_dev      channel device descriptor
   sch_unit     channel unit descriptor
   sch_mod      channel modifiers list
   sch_reg      channel register list
*/

DIB sch_dib = { d_SCH, -1, v_SCH, sch_tplte, &sch, &sch_ini };

UNIT sch_unit = { UDATA (NULL, 0, 0) };

REG sch_reg[] = {
    { HRDATA (CHAN, sch_max, 3), REG_HRO },
    { BRDATA (SA, sch_sa, 16, 20, SCH_NUMCH) },
    { BRDATA (EA, sch_ea, 16, 20, SCH_NUMCH) },
    { BRDATA (CMD, sch_cmd, 16, 8, SCH_NUMCH) },
    { BRDATA (DEV, sch_sdv, 16, 8, SCH_NUMCH) },
    { BRDATA (RDP, sch_rdp, 16, 2, SCH_NUMCH) },
    { BRDATA (WDC, sch_wdc, 16, 3, SCH_NUMCH) },
    { GRDATA (IREQ, int_req[l_SCH], 16, SCH_NUMCH, i_SCH) },
    { GRDATA (IENB, int_enb[l_SCH], 16, SCH_NUMCH, i_SCH) },
    { HRDATA (DEVNO, sch_dib.dno, 8), REG_HRO },
    { NULL }
    };

MTAB sch_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "channels", "CHANNELS",
      &sch_set_nchan, &sch_show_nchan, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "0", NULL,
      NULL, &sch_show_reg, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "1", NULL,
      NULL, &sch_show_reg, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 2, "2", NULL,
      NULL, &sch_show_reg, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 3, "3", NULL,
      NULL, &sch_show_reg, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, &sch_dib },
    { 0 }
    };

DEVICE sch_dev = {
    "SELCH", &sch_unit, sch_reg, sch_mod,
    1, 16, 8, 1, 16, 8,
    NULL, NULL, &sch_reset,
    NULL, NULL, NULL,
    &sch_dib, 0
    };

/* (Extended) selector channel

   There are really three different selector channels:
        - 16b selector channel (max 4B of data)
        - 18b selector channel (max 4B of data)
        - 20b selector channel (max 6B of data)
   The algorithm for loading the start and end addresses is taken
   from the maintenance manual for the Extended Selector Channel.
*/

#define SCH_EXR(ch)     ((sch_cmd[ch] & SCHC_EXA) && (pawidth == PAWIDTH32))

uint32 sch (uint32 dev, uint32 op, uint32 dat)
{
uint32 t, bank, sdv, ch = dev - sch_dib.dno;

switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        return BY;                                      /* byte only */

    case IO_RD:                                         /* read data */
        t = (sch_sa[ch] >> (sch_rdp[ch] * 8)) & DMASK8; /* get sa byte */
        if (sch_rdp[ch] == 0) sch_rdp[ch] =             /* wrap? */
            SCH_EXR (ch)? 2: 1;
        else sch_rdp[ch] = sch_rdp[ch] - 1;             /* dec byte ptr */
        return t;

    case IO_WD:                                         /* write data */
        if (pawidth != PAWIDTH32) {                     /* 16b? max 4 */
            if (sch_wdc[ch] >= 4)                       /* stop at 4 */
                break;
            sch_sa[ch] = ((sch_sa[ch] << 8) |           /* ripple ea to sa */
                (sch_ea[ch] >> 8)) & DMASK16;
            sch_ea[ch] = ((sch_ea[ch] << 8) |           /* ripple ea low */
                dat) & DMASK16;                         /* insert byte */
            }
        else {                                          /* 32b? max 6 */
            if (sch_wdc[ch] >= 6)                       /* stop at 6 */
                break;
            if (sch_wdc[ch] != 5) {                     /* if not last */
                sch_sa[ch] = ((sch_sa[ch] << 8) |       /* ripple ea<15:8> to sa */
                    ((sch_ea[ch] >> 8) & DMASK8)) & PAMASK32;
                sch_ea[ch] =                            /* ripple ea<7:0> */
                    (((sch_ea[ch] & DMASK8) << 8) | dat) & PAMASK32;
                }
            else sch_ea[ch] = ((sch_ea[ch] << 8) | dat) & PAMASK32;
            }
        sch_wdc[ch] = sch_wdc[ch] + 1;                  /* adv sequencer */
        break;

    case IO_SS:                                         /* status */
        if (sch_cmd[ch] & SCHC_GO)                      /* test busy */
            return STA_BSY;
        if (sch_cmd[ch] & SCHC_SSTA)                    /* test sch sta */
            return 0;
        else {
            sdv = sch_sdv[ch];                          /* get dev */
            if (dev_tab[sdv] == 0)                      /* not there? */
                return CC_V;
            dev_tab[sdv] (sdv, IO_ADR, 0);              /* select dev */
            t = dev_tab[sdv] (sdv, IO_SS, 0);           /* get status */
            return t & ~STA_BSY;                        /* clr busy */
            }

    case IO_OC:                                         /* command */
        bank = 0;                                       /* assume no bank */
        if (pawidth != PAWIDTH32) {                     /* 16b/18b proc? */
            dat = dat & ~(SCHC_EXA | SCHC_SSTA);        /* clr ext func */
            if (pawidth == PAWIDTH16E)                  /* 18b proc? */
                bank = (dat & SCHC_EXM) << 16;
            }
        if (dat & SCHC_STOP) {                          /* stop? */
            sch_cmd[ch] = dat & (SCHC_EXA | SCHC_SSTA); /* clr go */
            CLR_INT (v_SCH + ch);                       /* clr intr */
            sch_rdp[ch] = SCH_EXR (ch)? 2: 1;           /* init sequencers */
            sch_wdc[ch] = 0;
            }
        else if (dat & SCHC_GO) {                       /* go? */
            sch_cmd[ch] = dat & (SCHC_EXA | SCHC_SSTA| SCHC_GO | SCHC_RD);
            if (sch_wdc[ch] <= 4) {                     /* 4 bytes? */
                sch_sa[ch] = (sch_sa[ch] & PAMASK16) | bank;    /* 16b addr */
                sch_ea[ch] = (sch_ea[ch] & PAMASK16) | bank;
                }
            sch_sa[ch] = sch_sa[ch] & ~1;
            if (sch_ea[ch] <= sch_sa[ch])               /* wrap? */
                sch_ea[ch] = sch_ea[ch] |               /* modify end addr */
                ((pawidth == PAWIDTH32)? PAMASK32: PAMASK16);
            }
        break;
        }

return 0;
}

/* CPU call to test if channel blocks access to device */

t_bool sch_blk (uint32 dev)
{
uint32 ch = sch_tab[dev] - 1;

if ((ch < sch_max) && (sch_cmd[ch] & SCHC_GO))
    return TRUE;
return FALSE;
}

/* Device call to 'remember' last dev on channel */

void sch_adr (uint32 ch, uint32 dev)
{
if (ch < sch_max)
    sch_sdv[ch] = dev;
return;
}

/* Device call to see if selector channel is active for device */

t_bool sch_actv (uint32 ch, uint32 dev)
{
if ((ch < sch_max) &&                                   /* chan valid, */
    (sch_cmd[ch] & SCHC_GO) &&                          /* on, and */
    (sch_sdv[ch] == dev))                               /* set for dev? */
    return TRUE;
return FALSE;                                           /* no */
}

/* Device call to read a block of memory */

uint32 sch_rdmem (uint32 ch, uint8 *buf, uint32 cnt)
{
uint32 addr, end, xfr, inc;

if ((ch >= sch_max) || ((sch_cmd[ch] & SCHC_GO) == 0))
    return 0;
addr = sch_sa[ch];                                      /* start */
end = sch_ea[ch];                                       /* end */
xfr = MIN (cnt, end - addr + 1);                        /* sch xfr cnt */
inc = IOReadBlk (addr, xfr, buf);                       /* read mem */
if ((addr + inc) > end) {                               /* end? */
    SET_INT (v_SCH + ch);                               /* interrupt */
    sch_cmd[ch] &= ~(SCHC_GO | SCHC_RD);                /* clear GO */
    sch_sa[ch] = sch_sa[ch] + inc - 1;                  /* end addr */
    }
else sch_sa[ch] = sch_sa[ch] + inc;                     /* next addr */
return inc;
}

/* Device call to write a block of memory */

uint32 sch_wrmem (uint32 ch, uint8 *buf, uint32 cnt)
{
uint32 addr, end, xfr, inc;

if ((ch >= sch_max) || ((sch_cmd[ch] & SCHC_GO) == 0))
    return 0;
addr = sch_sa[ch];                                      /* start */
end = sch_ea[ch];                                       /* end */
xfr = MIN (cnt, end - addr + 1);                        /* sch xfr cnt */
inc = IOWriteBlk (addr, xfr, buf);                      /* write mem */
if ((addr + inc) > end) {                               /* end? */
    SET_INT (v_SCH + ch);                               /* interrupt */
    sch_cmd[ch] &= ~(SCHC_GO | SCHC_RD);                /* clear GO */
    sch_sa[ch] = sch_sa[ch] + inc - 1;                  /* end addr */
    }
else sch_sa[ch] = sch_sa[ch] + inc;                     /* next addr */
return inc;
}

/* Device call to stop a selector channel */

void sch_stop (uint32 ch)
{
if (ch < sch_max) {
    SET_INT (v_SCH + ch);                               /* interrupt */
    sch_cmd[ch] &= ~(SCHC_GO | SCHC_RD);                /* clear GO */
    }
return;
}

/* Reset */

void sch_reset_ch (uint32 rst_lim)
{
uint32 ch;

for (ch = 0; ch < SCH_NUMCH; ch++) {
    if (ch >= rst_lim) {
        CLR_INT (v_SCH + ch);
        SET_ENB (v_SCH + ch);
        sch_sa[ch] = sch_ea[ch] = 0;
        sch_cmd[ch] = sch_sdv[ch] = 0;
        sch_wdc[ch] = 0;
        sch_rdp[ch] = 1;
        }
    }
return;
}

t_stat sch_reset (DEVICE *dptr)
{
sch_reset_ch (0);                                       /* reset all chan */
return SCPE_OK;
}

/* Set number of channels */

t_stat sch_set_nchan (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 i, newmax;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newmax = get_uint (cptr, 10, SCH_NUMCH, &r);            /* get new max */
if ((r != SCPE_OK) || (newmax == sch_max))              /* err or no chg? */
    return r;
if (newmax == 0)                                        /* must be > 0 */
    return SCPE_ARG;
if (newmax < sch_max) {                                 /* reducing? */
    for (i = 0; (dptr = sim_devices[i]); i++) {           /* loop thru dev */
        dibp = (DIB *) dptr->ctxt;                      /* get DIB */
        if (dibp && (dibp->sch >= (int32) newmax)) {    /* dev using chan? */
            sim_printf ("Device %02X uses channel %d\n",
                    dibp->dno, dibp->sch);
            return SCPE_OK;
            }
        }
    }
sch_max = newmax;                                       /* set new max */
sch_reset_ch (sch_max);                                 /* reset chan */
return SCPE_OK;
}

/* Show number of channels */

t_stat sch_show_nchan (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "channels=%d", sch_max);
return SCPE_OK;
}

/* Show channel registers */

t_stat sch_show_reg (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (val < 0)
    return SCPE_IERR;
if (val >= (int32) sch_max)
    fprintf (st, "Channel %d disabled\n", val);
else {
    fprintf (st, "SA:   %05X\n", sch_sa[val]);
    fprintf (st, "EA:   %05X\n", sch_ea[val]);
    fprintf (st, "CMD:  %02X\n", sch_cmd[val]);
    fprintf (st, "DEV:  %02X\n", sch_sdv[val]);
    fprintf (st, "RDP:  %X\n", sch_rdp[val]);
    fprintf (st, "WDC:  %X\n", sch_wdc[val]);
    }
return SCPE_OK;
}

/* Initialize template */

void sch_ini (t_bool dtpl)
{
uint32 i;

for (i = 0; i < sch_max; i++)
    sch_tplte[i] = i;
sch_tplte[sch_max] = TPL_END;
return;
}

/* Evaluate interrupt */

void int_eval (void)
{
int i;
extern uint32 qevent;

for (i = 0; i < INTSZ; i++) {
    if (int_req[i] & int_enb[i]) {
        qevent = qevent | EV_INT;
        return;
        }
    }
qevent = qevent & ~EV_INT;
return;
}

/* Return interrupting device */

uint32 int_getdev (void)
{
int32 i, j, t;
uint32 r;

for (i = t = 0; i < INTSZ; i++) {                       /* loop thru array */
    if ((r = int_req[i] & int_enb[i])) {                /* find nz int wd */
        for (j = 0; j < 32; t++, j++) {
            if (r & (1u << j)) {
                int_req[i] = int_req[i] & ~(1u << j);   /* clr request */
                return int_tab[t];
                }
            }
        }
    else t = t + 32;
    }
return 0;
}

/* Update device interrupt status */

int32 int_chg (uint32 irq, int32 dat, int32 armdis)
{
int32 t = CMD_GETINT (dat);                             /* get int ctrl */
    
if (t == CMD_IENB) {                                    /* enable? */
    SET_ENB (irq);
    return 1;
    }
else if (t == CMD_IDIS) {                               /* disable? */
    CLR_ENB (irq);
    return 1;
    }
if (t == CMD_IDSA) {                                    /* disarm? */
    CLR_ENB (irq);
    CLR_INT (irq);
    return 0;
    }
return armdis;
}

/* Process a 2b field and return unchanged, set, clear, complement */

int32 io_2b (int32 val, int32 pos, int32 old)
{
int32 t = (val >> pos) & 3;
if (t == 0)
    return old;
if (t == 1)
    return 1;
if (t == 2)
    return 0;
return old ^1;
}

/* Block transfer routines */

uint32 IOReadBlk (uint32 loc, uint32 cnt, uint8 *buf)
{
uint32 i;

if (!MEM_ADDR_OK (loc) || (cnt == 0))
    return 0;
if (!MEM_ADDR_OK (loc + cnt - 1))
    cnt = MEMSIZE - loc;
for (i = 0; i < cnt; i++)
    buf[i] = IOReadB (loc + i);
return cnt;
}

uint32 IOWriteBlk (uint32 loc, uint32 cnt, uint8 *buf)
{
uint32 i;

if (!MEM_ADDR_OK (loc) || (cnt == 0))
    return 0;
if (!MEM_ADDR_OK (loc + cnt - 1))
    cnt = MEMSIZE - loc;
for (i = 0; i < cnt; i++)
    IOWriteB (loc + i, buf[i]);
return cnt;
}

/* Change selector channel for a device */

t_stat set_sch (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newch;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if ((dibp == NULL) || (dibp->sch < 0))
    return SCPE_IERR;
newch = get_uint (cptr, 16, sch_max - 1, &r);           /* get new */
if (r != SCPE_OK)
    return r;
dibp->sch = newch;                                      /* store */
return SCPE_OK;
}

/* Show selector channel for a device */

t_stat show_sch (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
DIB *dibp;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if ((dibp == NULL) || (dibp->sch < 0))
    return SCPE_IERR;
fprintf (st, "selch=%X", dibp->sch);
return SCPE_OK;
}

/* Change device number for a device */

t_stat set_dev (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newdev;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
newdev = get_uint (cptr, 16, DEV_MAX, &r);              /* get new */
if ((r != SCPE_OK) || (newdev == dibp->dno))
    return r;
if (newdev == 0)                                        /* must be > 0 */
    return SCPE_ARG;
dibp->dno = newdev;                                     /* store */
return SCPE_OK;
}

/* Show device number for a device */

t_stat show_dev (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
DIB *dibp;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if ((dibp == NULL) || (dibp->dno == 0))
    return SCPE_IERR;
fprintf (st, "devno=%02X", dibp->dno);
return SCPE_OK;
}

/* Init device tables */

t_bool devtab_init (void)
{
DEVICE *dptr;
DIB *dibp;
uint32 i, j, dno, dmsk, doff, t, dmap[DEVNO / 32];
uint8 *tplte, dflt_tplte[] = { 0, TPL_END };

/* Clear tables, device map */

for (i = 0; i < DEVNO; i++) {
    dev_tab[i] = NULL;
    sch_tab[i] = 0;
    }
for (i = 0; i < (INTSZ * 32); i++)
    int_tab[i] = 0;
for (i = 0; i < (DEVNO / 32); i++)
    dmap[i] = 0;

/* Test each device for conflict; add to map; init tables */

for (i = 0; (dptr = sim_devices[i]); i++) {               /* loop thru devices */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if ((dibp == NULL) || (dptr->flags & DEV_DIS))      /* exist, enabled? */
        continue;
    dno = dibp->dno;                                    /* get device num */
    if (dibp->ini)                                      /* gen dno template */
        dibp->ini (TRUE);
    tplte = dibp->tplte;                                /* get template */
    if (tplte == NULL)                                  /* none? use default */
        tplte = dflt_tplte;
    for ( ; *tplte != TPL_END; tplte++) {               /* loop thru template */
        t = (dno + *tplte) & DEV_MAX;                   /* loop thru template */
        dmsk = 1u << (t & 0x1F);                        /* bit to test */
        doff = t / 32;                                  /* word to test */
        if (dmap[doff] & dmsk) {                        /* in use? */
            sim_printf ("Device number conflict, devno = %02X\n", t);
            return TRUE;
            }
        dmap[doff] = dmap[doff] | dmsk;
        if (dibp->sch >= 0)
            sch_tab[t] = dibp->sch + 1;
        dev_tab[t] = dibp->iot;
        }
    if (dibp->ini)                                      /* gen int template */
        dibp->ini (FALSE);
    tplte = dibp->tplte;                                /* get template */
    if (tplte == NULL)                                  /* none? use default */
        tplte = dflt_tplte;
    for (j = dibp->irq; *tplte != TPL_END; j++, tplte++)
        int_tab[j] = (dno + *tplte) & DEV_MAX;
    }                                                   /* end for i */
return FALSE;
}
