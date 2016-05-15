/* sds_dsk.c: SDS 940 moving head disk simulator

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

   dsk          moving head disk

   The SDS 9164 disk has a subsector feature, allowing each 64W sector to be
   viewed as 16W packets.  In addition, it has a chaining feature, allowing
   records to be extended beyond a sector boundary.  To accomodate this, the
   first word of each sector has 3 extra bits:

   <26> =       end of chain flag       
   <25:24> =    4 - number of packets

   These values were chosen so that 000 = continue chain, full sector.
*/

#include "sds_defs.h"

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

#define DSK_PKTWD       16                              /* words/packet */
#define DSK_NUMPKT      4                               /* packets/sector */
#define DSK_NUMWD       (DSK_PKTWD*DSK_NUMPKT)          /* words/sector */
#define DSK_N_SC        5                               /* sect addr width */
#define DSK_V_SC        0                               /* position */
#define DSK_M_SC        ((1 << DSK_N_SC) - 1)           /* mask */
#define DSK_NUMSC       (1 << DSK_N_SC)                 /* sectors/track */
#define DSK_N_TR        8                               /* track addr width */
#define DSK_V_TR        (DSK_N_SC)                      /* position */
#define DSK_M_TR        ((1 << DSK_N_TR) - 1)           /* mask */
#define DSK_NUMTR       (1 << DSK_N_TR)                 /* tracks/surface */
#define DSK_N_SF        5                               /* surf addr width */
#define DSK_V_SF        (DSK_N_SC + DSK_N_TR)           /* position */
#define DSK_M_SF        ((1 << DSK_N_SF) - 1)           /* mask */
#define DSK_NUMSF       (1 << DSK_N_SF)                 /* surfaces/drive */
#define DSK_SCSIZE      (DSK_NUMSF*DSK_NUMTR*DSK_NUMSC) /* sectors/drive */
#define DSK_AMASK       (DSK_SCSIZE - 1)                /* address mask */
#define DSK_SIZE        (DSK_SCSIZE * DSK_NUMWD)        /* words/drive */
#define DSK_GETTR(x)    (((x) >> DSK_V_TR) & DSK_M_TR)
#define cyl             u3                              /* curr cylinder */
#define DSK_SIP         (1 << (DSK_N_TR + 2))
#define DSK_V_PKT       24
#define DSK_M_PKT       03
#define DSK_V_CHN       26
#define DSK_GETPKT(x)   (4 - (((x) >> DSK_V_PKT) & DSK_M_PKT))
#define DSK_ENDCHN(x)   ((x) & (1 << DSK_V_CHN))                

extern uint32 xfr_req;
extern uint32 alert;
extern int32 stop_invins, stop_invdev, stop_inviop;
int32 dsk_da = 0;                                       /* disk addr */
int32 dsk_op = 0;                                       /* operation */
int32 dsk_err = 0;                                      /* error flag */
uint32 dsk_buf[DSK_NUMWD];                              /* sector buf */
int32 dsk_bptr = 0;                                     /* byte ptr */
int32 dsk_blnt = 0;                                     /* byte lnt */
int32 dsk_time = 5;                                     /* time per char */
int32 dsk_stime = 200;                                  /* seek time */
int32 dsk_stopioe = 1;
DSPT dsk_tplt[] = {                                     /* template */
    { 1, 0 },
    { 1, DEV_OUT },
    { 0, 0 }
    };

t_stat dsk_svc (UNIT *uptr);
t_stat dsk_reset (DEVICE *dptr);
t_stat dsk_fill (uint32 dev);
t_stat dsk_read_buf (uint32 dev);
t_stat dsk_write_buf (uint32 dev);
void dsk_end_op (uint32 fl);
t_stat dsk (uint32 fnc, uint32 inst, uint32 *dat);

/* DSK data structures

   dsk_dev      device descriptor
   dsk_unit     unit descriptor
   dsk_reg      register list
*/

DIB dsk_dib = { CHAN_F, DEV_DSK, XFR_DSK, dsk_tplt, &dsk };

UNIT dsk_unit = { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE, DSK_SIZE) };

REG dsk_reg[] = {
    { BRDATA (BUF, dsk_buf, 8, 24, DSK_NUMWD) },
    { DRDATA (BPTR, dsk_bptr, 9), PV_LEFT },
    { DRDATA (BLNT, dsk_bptr, 9), PV_LEFT },
    { ORDATA (DA, dsk_da, 21) },
    { ORDATA (INST, dsk_op, 24) },
    { FLDATA (XFR, xfr_req, XFR_V_DSK) },
    { FLDATA (ERR, dsk_err, 0) },
    { DRDATA (WTIME, dsk_time, 24), REG_NZ + PV_LEFT },
    { DRDATA (STIME, dsk_stime,24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, dsk_stopioe, 0) },
    { NULL }
    };

MTAB dsk_mod[] = {
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", "CHANNEL",
      &set_chan, &show_chan, NULL },
    { 0 }
    };

DEVICE dsk_dev = {
    "DSK", &dsk_unit, dsk_reg, dsk_mod,
    1, 8, 24, 1, 8, 27,
    NULL, NULL, &dsk_reset,
    NULL, NULL, NULL,
    &dsk_dib, DEV_DISABLE
    };

/* Moving head disk routine

   conn -       inst = EOM0, dat = NULL
   eom1 -       inst = EOM1, dat = NULL
   sks -        inst = SKS, dat = ptr to result
   disc -       inst = device number, dat = NULL
   wreor -      inst = device number, dat = NULL
   read -       inst = device number, dat = ptr to data
   write -      inst = device number, dat = ptr to result
*/

t_stat dsk (uint32 fnc, uint32 inst, uint32 *dat)
{
int32 i, t, new_ch, dsk_wptr, dsk_byte;
t_stat r;

switch (fnc) {                                          /* case on function */

    case IO_CONN:                                       /* connect */
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != dsk_dib.chan)                     /* wrong chan? */
            return SCPE_IERR;
        dsk_op = inst;                                  /* save instr */
        dsk_bptr = dsk_blnt = 0;                        /* init ptrs */
        for (i = 0; i < DSK_NUMWD; i++)                 /* clear buffer */
            dsk_buf[i] = 0;
        xfr_req = xfr_req & ~XFR_DSK;                   /* clr xfr flg */
        sim_activate (&dsk_unit, dsk_stime);            /* activate */
        break;

    case IO_EOM1:                                       /* EOM mode 1 */
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != dsk_dib.chan)                     /* wrong chan? */
            return SCPE_IERR;
        if (inst & 07600)                               /* inv inst? */
            CRETIOP;
        alert = POT_DSK;                                /* alert */
        break;

    case IO_DISC:                                       /* disconnect */
        dsk_end_op (0);                                 /* normal term */
        if (inst & DEV_OUT)
            return dsk_fill (inst);                     /* fill write */
        break;

    case IO_WREOR:                                      /* write eor */
        dsk_end_op (CHF_EOR);                           /* eor term */
        return dsk_fill (inst);                         /* fill write */

    case IO_SKS:                                        /* SKS */
        new_ch = I_GETSKCH (inst);                      /* sks chan */
        if (new_ch != dsk_dib.chan)
            return SCPE_IERR;   /* wrong chan? */
        t = I_GETSKCND (inst);                          /* sks cond */
        if (((t == 000) && !sim_is_active (&dsk_unit) &&/* 10026: ready  */
             (dsk_unit.flags & UNIT_ATT)) ||
            ((t == 004) && !dsk_err &&                  /* 11026: !err */
             (dsk_unit.flags & UNIT_ATT)) ||
            ((t == 010) && ((dsk_unit.cyl & DSK_SIP) == 0)) || /* 12026: on trk */
            ((t == 014) && !(dsk_unit.flags & UNIT_WPRT)) ||   /* 13026: !wrprot */
            ((t == 001) && (dsk_unit.flags & UNIT_ATT)))       /* 10226: online */
            *dat = 1;
        break;

    case IO_READ:
        xfr_req = xfr_req & ~XFR_DSK;                   /* clr xfr req */
        if (dsk_bptr >= dsk_blnt) {                     /* no more data? */
            if ((r = dsk_read_buf (inst)))              /* read sector */
                return r;
            }
        dsk_wptr = dsk_bptr >> 2;                       /* word pointer */
        dsk_byte = dsk_bptr & 03;                       /* byte in word */
        *dat = (dsk_buf[dsk_wptr] >> ((3 - dsk_byte) * 6)) & 077;
        dsk_bptr = dsk_bptr + 1;                        /* incr buf ptr */
        if ((dsk_bptr >= dsk_blnt) &&                   /* end sector, */
            ((dsk_op & CHC_BIN) || DSK_ENDCHN (dsk_buf[0])))/* sec mode | eoch? */
            dsk_end_op (CHF_EOR);                       /* eor term */
        break;

    case IO_WRITE:
        xfr_req = xfr_req & ~XFR_DSK;                   /* clr xfr req */
        if (dsk_bptr >= (DSK_NUMWD * 4)) {              /* full? */
            if ((r = dsk_write_buf (inst)))             /* write sector */
                return r;
            }
        dsk_wptr = dsk_bptr >> 2;                       /* word pointer */
        dsk_buf[dsk_wptr] = ((dsk_buf[dsk_wptr] << 6) | (*dat & 077)) & DMASK;
        dsk_bptr = dsk_bptr + 1;                        /* incr buf ptr */
        break;

    default:
        CRETINS;
        }

return SCPE_OK; 
}

/* PIN routine - return disk address */

t_stat pin_dsk (uint32 num, uint32 *dat)
{
*dat = dsk_da;                                          /* ret disk addr */
return SCPE_OK;
}

/* POT routine - start seek */

t_stat pot_dsk (uint32 num, uint32 *dat)
{
int32 st;

if (sim_is_active (&dsk_unit))                          /* busy? wait */
    return STOP_IONRDY;
dsk_da = (*dat) & DSK_AMASK;                            /* save dsk addr */
st = abs (DSK_GETTR (dsk_da) -                          /* calc seek time */
     (dsk_unit.cyl & DSK_M_TR)) * dsk_stime;
if (st == 0                                             /* min time */
        ) st = dsk_stime;
sim_activate (&dsk_unit, st);                           /* set timer */
dsk_unit.cyl = dsk_unit.cyl | DSK_SIP;                  /* seeking */
return SCPE_OK;
}

/* Unit service and read/write */

t_stat dsk_svc (UNIT *uptr)
{
if (uptr->cyl & DSK_SIP) {                              /* end seek? */
    uptr->cyl = DSK_GETTR (dsk_da);                     /* on cylinder */
    if (dsk_op)                                         /* sched r/w */
        sim_activate (&dsk_unit, dsk_stime);
    }
else {
    xfr_req = xfr_req | XFR_DSK;                        /* set xfr req */
    sim_activate (&dsk_unit, dsk_time);                 /* activate */
    }
return SCPE_OK;
}

/* Read sector */

t_stat dsk_read_buf (uint32 dev)
{
int32 da, pkts, awc;

if ((dsk_unit.flags & UNIT_ATT) == 0) {                 /* !attached? */
    dsk_end_op (CHF_ERR | CHF_EOR);                     /* disk error */
    CRETIOE (dsk_stopioe, SCPE_UNATT);
    }
da = dsk_da * DSK_NUMWD * sizeof (uint32);
fseek (dsk_unit.fileref, da, SEEK_SET);                 /* locate sector */
awc = fxread (dsk_buf, sizeof (uint32), DSK_NUMWD, dsk_unit.fileref);
if (ferror (dsk_unit.fileref)) {                        /* error? */
    dsk_end_op (CHF_ERR | CHF_EOR);                     /* disk error */
    return SCPE_IOERR;
    }
for ( ; awc < DSK_NUMWD; awc++)
    dsk_buf[awc] = 0;
pkts = DSK_GETPKT (dsk_buf[0]);                         /* get packets */
dsk_blnt = pkts * DSK_PKTWD * 4;                        /* new buf size */
dsk_bptr = 0;                                           /* init bptr */
dsk_da = (dsk_da + 1) & DSK_AMASK;                      /* incr disk addr */
return SCPE_OK;
}

/* Write sector.  If this routine is called directly, then the sector
   buffer is full, and there is at least one more character to write;
   therefore, there are 4 packets in the sector, and the sector is not
   the end of the chain.
*/

t_stat dsk_write_buf (uint32 dev)
{
int32 i, da;

if ((dsk_unit.flags & UNIT_ATT) == 0) {                 /* !attached? */
    dsk_end_op (CHF_ERR | CHF_EOR);                     /* disk error */
    CRETIOE (dsk_stopioe, SCPE_UNATT);
    }
if (dsk_unit.flags & UNIT_WPRT) {                       /* write prot? */
    dsk_end_op (CHF_ERR | CHF_EOR);                     /* disk error */
    return SCPE_OK;
    }
da = dsk_da * DSK_NUMWD * sizeof (uint32);
fseek (dsk_unit.fileref, da, SEEK_SET);                 /* locate sector */
fxwrite (dsk_buf, sizeof (uint32), DSK_NUMWD, dsk_unit.fileref);
if (ferror (dsk_unit.fileref)) {                        /* error? */
    dsk_end_op (CHF_ERR | CHF_EOR);                     /* disk error */
    return SCPE_IOERR;
    }
dsk_bptr = 0;                                           /* init bptr */
dsk_da = (dsk_da + 1) & DSK_AMASK;                      /* incr disk addr */
for (i = 0; i < DSK_NUMWD; i++)                         /* clear buffer */
    dsk_buf[i] = 0;
return SCPE_OK;
}

/* Fill incomplete sector at end of operation.  Calculate the number
   of packets and set the end of chain flag.
*/

t_stat dsk_fill (uint32 dev)
{
int32 nochn = (dsk_op & CHC_BIN)? 0: 1;                 /* chain? */
int32 pktend = (dsk_bptr + ((DSK_PKTWD * 4) - 1)) &     /* end pkt */
    ~((DSK_PKTWD * 4) - 1);
int32 pkts = pktend / (DSK_PKTWD * 4);                  /* # packets */

if (dsk_bptr == 0)                                      /* no fill? */
    return SCPE_OK;
for ( ; dsk_bptr < pktend; dsk_bptr++) {                /* fill packet */
    int32 dsk_wptr = dsk_bptr >> 2;
    dsk_buf[dsk_wptr] = (dsk_buf[dsk_wptr] << 6) & DMASK;
    }
dsk_buf[0] = dsk_buf[0] | (nochn << DSK_V_CHN) |        /* insert chain, */
    ((4 - pkts) << DSK_V_PKT);                          /* num pkts */
return dsk_write_buf (dev);                             /* write sec */
}

/* Terminate DSK operation */

void dsk_end_op (uint32 fl)
{
if (fl)                                                 /* set flags */
    chan_set_flag (dsk_dib.chan, fl);
dsk_op = 0;                                             /* clear op */
xfr_req = xfr_req & ~XFR_DSK;                           /* clear xfr */
sim_cancel (&dsk_unit);                                 /* stop */
if (fl & CHF_ERR) {                                     /* error? */
    chan_disc (dsk_dib.chan);                           /* disconnect */
    dsk_err = 1;                                        /* set disk err */
    }
return;
}

/* Reset routine */

t_stat dsk_reset (DEVICE *dptr)
{
int32 i;

chan_disc (dsk_dib.chan);                               /* disconnect */
dsk_da = 0;                                             /* clear state */
dsk_op = 0;
dsk_err = 0;
dsk_bptr = dsk_blnt = 0;
xfr_req = xfr_req & ~XFR_DSK;                           /* clr xfr req */
sim_cancel (&dsk_unit);                                 /* deactivate */
dsk_unit.cyl = 0;
for (i = 0; i < DSK_NUMWD; i++)                         /* clear buffer */
    dsk_buf[i] = 0;
return SCPE_OK;
}
