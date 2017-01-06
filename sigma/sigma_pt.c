/* sigma_pt.c: Sigma 7060 paper tape reader/punch

   Copyright (c) 2007-2008, Robert M. Supnik

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

   pt           7060 paper-tape reader/punch
*/

#include "sigma_io_defs.h"

/* Device definitions */

#define PTR             0
#define PTP             1

/* Device states */

#define PTS_INIT        0x101
#define PTS_END         0x102
#define PTS_WRITE       0x1
#define PTS_READ        0x2
#define PTS_READI       0x82

/* Device status */

#define PTDV_PMAN       0x20
#define PTDV_RMAN       0x10

uint32 pt_cmd = 0;
uint32 ptr_nzc = 0;
uint32 ptr_stopioe = 1;
uint32 ptp_stopioe = 1;

extern uint32 chan_ctl_time;
extern uint8 ascii_to_ebcdic[128];
extern uint8 ebcdic_to_ascii[256];

uint32 pt_disp (uint32 op, uint32 dva, uint32 *dvst);
uint32 pt_tio_status (void);
uint32 pt_tdv_status (void);
t_stat pt_chan_err (uint32 st);
t_stat pt_svc (UNIT *uptr);
t_stat pt_reset (DEVICE *dptr);
t_stat pt_attach (UNIT *uptr, CONST char *cptr);

/* PT data structures

   pt_dev       PT device descriptor
   pt_unit      PT unit descriptors
   pt_reg       PT register list
   pt_mod       PT modifiers list
*/

dib_t pt_dib = { DVA_PT, pt_disp };

UNIT pt_unit[] = {
    { UDATA (&pt_svc, UNIT_ATTABLE+UNIT_SEQ+UNIT_ROABLE, 0), SERIAL_IN_WAIT },
    { UDATA (&pt_svc, UNIT_ATTABLE+UNIT_SEQ, 0), SERIAL_OUT_WAIT }
    };

REG pt_reg[] = {
    { HRDATA (CMD, pt_cmd, 9) },
    { FLDATA (NZC, ptr_nzc,0) },
    { DRDATA (RPOS, pt_unit[PTR].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (RTIME, pt_unit[PTR].wait, 24), PV_LEFT },
    { FLDATA (RSTOP_IOE, ptr_stopioe, 0) },
    { DRDATA (PPOS, pt_unit[PTP].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (PTIME, pt_unit[PTP].wait, 24), REG_NZ + PV_LEFT },
    { FLDATA (PSTOP_IOE, ptp_stopioe, 0) },
    { HRDATA (DEVNO, pt_dib.dva, 12), REG_HRO },
    { NULL }
    };

MTAB pt_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "CHAN", "CHAN",
      &io_set_dvc, &io_show_dvc, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DVA", "DVA",
      &io_set_dva, &io_show_dva, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "CSTATE", NULL,
      NULL, &io_show_cst, NULL },
    { 0 }
    };

DEVICE pt_dev = {
    "PT", pt_unit, pt_reg, pt_mod,
    2, 10, 31, 1, 16, 8,
    NULL, NULL, &pt_reset,
    &io_boot, &pt_attach, NULL,
    &pt_dib, DEV_DISABLE
    };

/* Reader/punch: IO dispatch routine */

uint32 pt_disp (uint32 op, uint32 dva, uint32 *dvst)
{
switch (op) {                                           /* case on op */

    case OP_SIO:                                        /* start I/O */
        *dvst = pt_tio_status ();                       /* get status */
        if ((*dvst & DVS_DST) == 0) {                   /* idle? */
            pt_cmd = PTS_INIT;                          /* start dev thread */
            sim_activate (&pt_unit[PTR], chan_ctl_time);
            }
        break;

    case OP_TIO:                                        /* test status */
        *dvst = pt_tio_status ();                       /* return status */
        break;

    case OP_TDV:                                        /* test status */
        *dvst = pt_tdv_status ();                       /* return status */
        break;

    case OP_HIO:                                        /* halt I/O */
        chan_clr_chi (pt_dib.dva);                      /* clr int*/
        *dvst = pt_tio_status ();                       /* get status */
        if ((*dvst & DVS_DST) != 0) {                   /* busy? */
            sim_cancel (&pt_unit[PTR]);                 /* stop dev thread */
            chan_uen (pt_dib.dva);                      /* uend */
            }
        break;

    case OP_AIO:                                        /* acknowledge int */
        chan_clr_chi (pt_dib.dva);                      /* clr int*/
        *dvst = 0;                                      /* no status */
        break;

    default:
        *dvst = 0;
        return SCPE_IERR;
        }

return 0;
}

/* Service routine */

t_stat pt_svc (UNIT *uptr)
{
int32 c;
uint32 cmd;
uint32 st;

switch (pt_cmd) {                                       /* case on state */

    case PTS_INIT:                                      /* I/O init */
        st = chan_get_cmd (pt_dib.dva, &cmd);           /* get command */
        if (CHS_IFERR (st))                             /* channel error? */
            return pt_chan_err (st);
        if ((cmd == PTS_WRITE) ||                       /* valid command? */
            ((cmd & 0x7F) == PTS_READ))
            pt_cmd = cmd;                               /* next state */
        else pt_cmd = PTS_END;                          /* no, end state */
        sim_activate (uptr, chan_ctl_time);             /* continue thread */
        break;

    case PTS_READ:
    case PTS_READI:
        sim_activate (uptr, uptr->wait);                /* continue thread */
        if ((uptr->flags & UNIT_ATT) == 0)              /* not attached? */
            return ptr_stopioe? SCPE_UNATT: SCPE_OK;
        if ((c = getc (uptr->fileref)) == EOF) {        /* read char */
            if (feof (uptr->fileref)) {                 /* end of file? */
                chan_set_chf (pt_dib.dva, CHF_LNTE);    /* length error */
                pt_cmd = PTS_END;                       /* end state */
                break;
                }
            else {                                      /* real error */
                sim_perror ("PTR I/O error");
                clearerr (uptr->fileref);
                chan_set_chf (pt_dib.dva, CHF_XMDE);    /* data error */
                return pt_chan_err (SCPE_IOERR);        /* force uend */
               }
            }
        uptr->pos = uptr->pos + 1;
        if (c != 0)                                     /* leader done? */
            ptr_nzc = 1;                                /* set flag */
        if ((pt_cmd == PTS_READI) || ptr_nzc) {
            st = chan_WrMemB (pt_dib.dva, c);           /* write to memory */
            if (CHS_IFERR (st))                         /* channel error? */
                return pt_chan_err (st);
            if (st == CHS_ZBC)                          /* bc == 0? */
                pt_cmd = PTS_END;                       /* end state */
            }
        break;

    case PTS_WRITE:                                     /* write */
        sim_activate (uptr, pt_unit[PTP].wait);         /* continue thread */
        if ((pt_unit[PTP].flags & UNIT_ATT) == 0)       /* not attached? */
            return ptp_stopioe? SCPE_UNATT: SCPE_OK;
        st = chan_RdMemB (pt_dib.dva, (uint32 *)&c);    /* read from channel */
        if (CHS_IFERR (st))                             /* channel error? */
            return pt_chan_err (st);
        if (putc (c, pt_unit[PTP].fileref) == EOF) {
            sim_perror ("PTP I/O error");
            clearerr (pt_unit[PTP].fileref);
            chan_set_chf (pt_dib.dva, CHF_XMDE);        /* data error */
            return pt_chan_err (SCPE_IOERR);            /* force uend */
            }
        pt_unit[PTP].pos = pt_unit[PTP].pos + 1;
        if (st == CHS_ZBC)                              /* bc == 0? */
            pt_cmd = PTS_END;                           /* end state */
        break;

    case PTS_END:                                       /* command done */
        st = chan_end (pt_dib.dva);                     /* set channel end */
        if (CHS_IFERR (st))                             /* channel error? */
            return pt_chan_err (st);
        if (st == CHS_CCH) {                            /* command chain? */
            pt_cmd = PTS_INIT;                          /* restart thread */
            sim_activate (uptr, chan_ctl_time);
            }
        break;
        }

return SCPE_OK;
}

/* PT status routine */

uint32 pt_tio_status (void)
{
uint32 st;

if (((pt_unit[PTR].flags & UNIT_ATT) == 0) ||           /* rdr not att? */
    ((pt_unit[PTP].flags & UNIT_ATT) == 0))             /* pun not att? */
    st = 0;
else st = DVS_AUTO;                                     /* otherwise ok */
if (sim_is_active (&pt_unit[PTR]))                      /* dev busy? */
    st |= (DVS_CBUSY | DVS_DBUSY | (CC2 << DVT_V_CC));
return st;
}

uint32 pt_tdv_status (void)
{
uint32 st;

st = 0;
if ((pt_unit[PTR].flags & UNIT_ATT) == 0)               /* rdr not att? */
    st |= PTDV_RMAN;
if ((pt_unit[PTP].flags & UNIT_ATT) == 0)               /* pun not att? */
    st |= PTDV_PMAN;
return st;
}

/* Channel error */

t_stat pt_chan_err (uint32 st)
{
sim_cancel (&pt_unit[PTR]);                             /* stop dev thread */
chan_uen (pt_dib.dva);                                  /* uend */
if (st < CHS_ERR)
    return st;
return SCPE_OK;
}

/* Reset routine */

t_stat pt_reset (DEVICE *dptr)
{
sim_cancel (&pt_unit[PTR]);                            /* stop dev thread */
pt_cmd = 0;
chan_reset_dev (pt_dib.dva);                           /* clr int, active */
return SCPE_OK;
}

/* Attach routine */

t_stat pt_attach (UNIT *uptr, CONST char *cptr)
{
t_stat st;

st = attach_unit (uptr, cptr);
if ((uptr == &pt_unit[PTR]) && (st == SCPE_OK))
    ptr_nzc = 0;
return st;
}