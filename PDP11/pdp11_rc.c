/* pdp11_rc.c: RC11/RS64 fixed head disk simulator

   Copyright (c) 2007-2013, John A. Dundas III

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

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   rc           RC11/RS64 fixed head disk

   03-Dec-13    RMS     Added explicit void * cast
   28-Dec-07    JAD     Correct extraction of unit number from da in rc_svc.
                        Clear _all_ error bits when a new operation starts.
                        Passes all diagnostics in all configurations.
   25-Dec-07    JAD     Compute the CRC-16 of the last sector read via
                        a READ or WCHK.
   20-Dec-07    JAD     Correctly simulate rotation over the selected
                        track for RCLA.  Also update the register
                        correctly during I/O operations.
                        Insure function activation time is non-zero.
                        Handle unit number wrap correctly.
   19-Dec-07    JAD     Iterate over a full sector regardless of the
                        actual word count so that RCDA ends correctly.
                        Honor the read-only vs. read-write status of the
                        attached file.
   16-Dec-07    JAD     The RCDA must be checked for validity when it is
                        written to, not just when GO is received.
   15-Dec-07    JAD     Better handling of disk address errors and the RCLA
                        register.
                        Add more registers to the visible device state.
   07-Jan-07    JAD     Initial creation and testing.  Adapted from pdp11_rf.c.

   The RS64 is a head-per-track disk.  To minimize overhead, the entire RC11
   is buffered in memory.  Up to 4 RS64 "platters" may be controlled by one
   RC11 for a total of 262,144 words (65536kW/platter).  [Later in time the
   RK611 was assigned the same CSR address.]

   Diagnostic routines:
     ZRCAB0.BIC - passes w/1-4 platters
     ZRCBB0.BIC - passes w/1-4 platters
     ZRCCB0.BIC - passes w/1-4 platters
   Note that the diagnostics require R/W disks (i.e., will destroy any
   existing data).

   For regression, must pass all three diagnostics configured for 1-4
   platters for a total of 12 tests.

   Information necessary to create this simulation was gathered from the
   PDP11 Peripherals Handbook, 1973-74 edition.

   One timing parameter is provided:

   rc_time      Minimum I/O operation time, must be non-zero
*/

#if !defined (VM_PDP11)
#error "RC11 is not supported!"
#endif
#include "pdp11_defs.h"
#include <math.h>

#define UNIT_V_AUTO     (UNIT_V_UF + 0)                 /* autosize */
#define UNIT_V_PLAT     (UNIT_V_UF + 1)                 /* #platters - 1 */
#define UNIT_M_PLAT     03
#define UNIT_GETP(x)    ((((x) >> UNIT_V_PLAT) & UNIT_M_PLAT) + 1)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_PLAT       (UNIT_M_PLAT << UNIT_V_PLAT)

/* Constants */

#define RC_NUMWD        (32*64)                         /* words/track */
#define RC_NUMTR        32                              /* tracks/disk */
#define RC_DKSIZE       (RC_NUMTR * RC_NUMWD)           /* words/disk */
#define RC_NUMDK        4                               /* disks/controller */
#define RC_WMASK        (RC_NUMWD - 1)                  /* word mask */

/* Parameters in the unit descriptor */

#define FUNC            u4                              /* function */

/* Control and status register (RCCS) */

#define RCCS_ERR        (CSR_ERR)                       /* error */
#define RCCS_DATA       0040000                         /* data error */
#define RCCS_ADDR       0020000                         /* address error */
#define RCCS_WLK        0010000                         /* write lock */
#define RCCS_NED        0004000                         /* nx disk */
#define RCCS_WCHK       0002000                         /* write check */
#define RCCS_INH        0001000                         /* inhibit CA incr */
#define RCCS_ABO        0000400                         /* abort */
#define RCCS_DONE       (CSR_DONE)
#define RCCS_IE         (CSR_IE)
#define RCCS_M_MEX      0000003                         /* memory extension */
#define RCCS_V_MEX      4
#define RCCS_MEX        (RCCS_M_MEX << RCCS_V_MEX)
#define RCCS_MAINT      0000010                         /* maint */
#define RCCS_M_FUNC     0000003                         /* function */
#define  RFNC_LAH       0
#define  RFNC_WRITE     1
#define  RFNC_READ      2
#define  RFNC_WCHK      3
#define RCCS_V_FUNC     1
#define RCCS_FUNC       (RCCS_M_FUNC << RCCS_V_FUNC)
#define RCCS_GO         0000001

#define RCCS_ALLERR     (RCCS_DATA|RCCS_ADDR|RCCS_WLK|RCCS_NED|RCCS_WCHK)
#define RCCS_W          (RCCS_INH | RCCS_ABO |RCCS_IE | RCCS_MEX | RCCS_MAINT | \
                         RCCS_FUNC | RCCS_GO)

/* Disk error status register (RCER) */

#define RCER_DLT        0100000                         /* data late */
#define RCER_CHK        0040000                         /* block check */
#define RCER_SYNC       0020000                         /* data sync */
#define RCER_NXM        0010000                         /* nonexistant memory */
#define RCER_TRK        0001000                         /* track error */
#define RCER_APAR       0000200                         /* address parity */
#define RCER_SADDR      0000100                         /* sync address */
#define RCER_OVFL       0000040                         /* disk overflow */
#define RCER_MIS        0000020                         /* missed transfer */

/* Lood Ahead Register (RCLA) */

#define RCLA_BADD       0100000                         /* bad address */

/* extract device operation code */
#define GET_FUNC(x)     (((x) >> RCCS_V_FUNC) & RCCS_M_FUNC)
/* extract memory extension address (bits 17,18) */
#define GET_MEX(x)      (((x) & RCCS_MEX) << (16 - RCCS_V_MEX))
#define GET_POS(x)      ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) RC_NUMWD)))

extern int32 int_req[IPL_HLVL];
extern int32 R[];

static uint32   rc_la = 0;                              /* look-ahead */
static uint32   rc_da = 0;                              /* disk address */
static uint32   rc_er = 0;                              /* error status */
static uint32   rc_cs = 0;                              /* command and status */
static uint32   rc_wc = 0;                              /* word count */
static uint32   rc_ca = 0;                              /* current address */
static uint32   rc_maint = 0;                           /* maintenance */
static uint32   rc_db = 0;                              /* data buffer */
static uint32   rc_wlk = 0;                             /* write lock */
static uint32   rc_time = 16;                           /* inter-word time: 16us */
static uint32   rc_stopioe = 1;                         /* stop on error */

/* forward references */

static t_stat rc_rd (int32 *, int32, int32);
static t_stat rc_wr (int32, int32, int32);
static t_stat rc_svc (UNIT *);
static t_stat rc_reset (DEVICE *);
static t_stat rc_attach (UNIT *, CONST char *);
static t_stat rc_set_size (UNIT *, int32, CONST char *, void *);
static uint32 update_rccs (uint32, uint32);
static const char *rc_description (DEVICE *dptr);

/* RC11 data structures

   rc_dev       RC device descriptor
   rc_unit      RC unit descriptor
   rc_reg       RC register list
*/

#define IOLN_RC         020

static DIB rc_dib = {
    IOBA_AUTO,
    IOLN_RC,
    &rc_rd,
    &rc_wr,
    1, IVCL (RC), VEC_AUTO, { NULL }, IOLN_RC,
};

static UNIT rc_unit = {
    UDATA (&rc_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_BUFABLE +
            UNIT_MUSTBUF + UNIT_ROABLE + UNIT_BINK, RC_DKSIZE)
};

static const REG rc_reg[] = {
    { ORDATA (RCLA, rc_la, 16) },
    { ORDATA (RCDA, rc_da, 16) },
    { ORDATA (RCER, rc_er, 16) },
    { ORDATA (RCCS, rc_cs, 16) },
    { ORDATA (RCWC, rc_wc, 16) },
    { ORDATA (RCCA, rc_ca, 16) },
    { ORDATA (RCMN, rc_maint, 16) },
    { ORDATA (RCDB, rc_db, 16) },
    { ORDATA (RCWLK, rc_wlk, 32) },
    { FLDATA (INT, IREQ (RC), INT_V_RC) },
    { FLDATA (ERR, rc_cs, CSR_V_ERR) },
    { FLDATA (DONE, rc_cs, CSR_V_DONE) },
    { FLDATA (IE, rc_cs, CSR_V_IE) },
    { DRDATA (TIME, rc_time, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, rc_stopioe, 0) },
    { ORDATA (DEVADDR, rc_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, rc_dib.vec, 16), REG_HRO },
    { NULL }
};

static const MTAB rc_mod[] = {
    { UNIT_PLAT, (0 << UNIT_V_PLAT), NULL, "1P", 
        &rc_set_size, NULL, NULL, "Set to 1 platter device" },
    { UNIT_PLAT, (1 << UNIT_V_PLAT), NULL, "2P",
        &rc_set_size, NULL, NULL, "Set to 2 platter device" },
    { UNIT_PLAT, (2 << UNIT_V_PLAT), NULL, "3P",
        &rc_set_size, NULL, NULL, "Set to 3 platter device" },
    { UNIT_PLAT, (3 << UNIT_V_PLAT), NULL, "4P",
        &rc_set_size, NULL, NULL, "Set to 4 platter device" },
    { UNIT_AUTO, UNIT_AUTO, "autosize", "AUTOSIZE", 
        NULL, NULL, NULL, "set platters based on file size at ATTACH" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0020, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,    0, "VECTOR", "VECTOR",
      &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 }
};

DEVICE rc_dev = {
    "RC", &rc_unit, (REG *) rc_reg, (MTAB *) rc_mod,
    1, 8, 21, 1, 8, 16,
    NULL,                                               /* examine */
    NULL,                                               /* deposit */
    &rc_reset,                                          /* reset */
    NULL,                                               /* boot */
    &rc_attach,                                         /* attach */
    NULL,                                               /* detach */
    &rc_dib,
    DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, 
    &rc_description
};

/* I/O dispatch routine, I/O addresses 17777440 - 17777456 */

static t_stat rc_rd (int32 *data, int32 PA, int32 access)
{
    uint32      t;

    switch ((PA >> 1) & 07) {                           /* decode PA<3:1> */

        case 0:                                         /* RCLA */
            t = rc_la & 017777;
            if ((rc_cs & RCCS_NED) || (rc_er & RCER_OVFL))
                t |= RCLA_BADD;
            *data = t;
            /* simulate sequential rotation about the current track */
            rc_la = (rc_la & ~077) | ((rc_la + 1) & 077);
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC rd: RCLA %06o\n", rc_la);
            break;

        case 1:                                         /* RCDA */
            *data = rc_da;
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC rd: RCDA %06o, PC %06o\n",
                         rc_da, PC);
            break;

        case 2:                                         /* RCER */
            *data = rc_er;
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC rd: RCER %06o\n", rc_er);
            break;

        case 3:                                         /* RCCS */
            *data = update_rccs (0, 0) & ~(RCCS_ABO | RCCS_GO);
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC rd: RCCS %06o\n", *data);
            break;

        case 4:                                         /* RCWC */
            *data = rc_wc;
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC rd: RCWC %06o\n", rc_wc);
            break;

        case 5:                                         /* RCCA */
            *data = rc_ca;
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC rd: RCCA %06o\n", rc_ca);
            break;

        case 6:                                         /* RCMN */
            *data = rc_maint;
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC rd: RCMN %06o\n", rc_maint);
            break;

        case 7:                                         /* RCDB */
            *data = rc_db;
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC rd: RCDB %06o\n", rc_db);
            break;

        default:
            return (SCPE_NXM);                          /* can't happen */
    }                                                   /* end switch */
    return (SCPE_OK);
}

static t_stat rc_wr (int32 data, int32 PA, int32 access)
{
    int32       t;

    switch ((PA >> 1) & 07) {                           /* decode PA<3:1> */ 

        case 0:                                         /* RCLA */
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC wr: RCLA\n");
            break;                                      /* read only */

        case 1:                                         /* RCDA */
            if (access == WRITEB)
                data = (PA & 1) ?
                    (rc_da & 0377) | (data << 8) :
                    (rc_da & ~0377) | data;
            rc_da = data & 017777;
            rc_cs &= ~RCCS_NED;
            update_rccs (0, 0);
            /* perform unit select */
            if (((rc_da >> 11) & 03) >= UNIT_GETP(rc_unit.flags))
                update_rccs (RCCS_NED, 0);
            else
                rc_la = rc_da;
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC wr: RCDA %06o, PC %06o\n",
                         rc_da, PC);
            break;

        case 2:                                         /* RCER */
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC wr: RCER\n");
            break;                                      /* read only */

        case 3:                                         /* RCCS */
            if (access == WRITEB)
                data = (PA & 1) ?
                    (rc_cs & 0377) | (data << 8) :
                    (rc_cs & ~0377) | data;
            if (data & RCCS_ABO) {
                update_rccs (RCCS_DONE, 0);
                sim_cancel (&rc_unit);
            }
            if ((data & RCCS_IE) == 0)                  /* int disable? */
                CLR_INT (RC);                           /* clr int request */
            else if ((rc_cs & (RCCS_DONE | RCCS_IE)) == RCCS_DONE)
                SET_INT (RC);                           /* set int request */
            rc_cs = (rc_cs & ~RCCS_W) | (data & RCCS_W); /* merge */
            if ((rc_cs & RCCS_DONE) && (data & RCCS_GO)) { /* new function? */
                rc_unit.FUNC = GET_FUNC (data);         /* save function */
                t = (rc_da & RC_WMASK) - GET_POS (rc_time); /* delta to new loc */
                if (t <= 0)                             /* wrap around? */
                    t = t + RC_NUMWD;
                sim_activate (&rc_unit, t * rc_time);   /* schedule op */
                /* clear error indicators for new operation */
                rc_cs &= ~(RCCS_ALLERR | RCCS_ERR | RCCS_DONE);
                rc_er = 0;
                CLR_INT (RC);
                if (DEBUG_PRS (rc_dev))
                    fprintf (sim_deb, ">>RC start: cs = %o, da = %o, ma = %o, wc = %o\n",
                             update_rccs (0, 0), rc_da,
                             GET_MEX (rc_cs) | rc_ca, rc_wc);
            }
            break;

        case 4:                                         /* RCWC */
            if (access == WRITEB)
                data = (PA & 1) ?
                   (rc_wc & 0377) | (data << 8) :
                   (rc_wc & ~0377) | data;
            rc_wc = data & DMASK;
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC wr: RCWC %06o, PC %06o\n",
                         rc_wc, PC);
            break;

        case 5:                                         /* RCCA */
                                                        /* TBD: write byte fixup? */
            rc_ca = data & 0177776;
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC wr: RCCA %06o\n", rc_ca);
            break;

        case 6:                                         /* RCMN */
                                                        /* TBD: write byte fixup? */
            rc_maint = data & 0177700;
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC wr: RCMN %06o\n", rc_maint);
            break;

        case 7:                                         /* RCDB */
            if (DEBUG_PRS (rc_dev))
                fprintf (sim_deb, ">>RC wr: RCDB\n");
            break;                                      /* read only */

        default:                                        /* can't happen */
            return (SCPE_NXM);
    }                                                   /* end switch */
    update_rccs (0, 0);
    return (SCPE_OK);
}

/* sector (32W) CRC-16 */

static uint32 sectorCRC (const uint16 *data)
{
    uint32      crc, i, j, d;

    crc = 0;
    for (i = 0; i < 32; i++) {
        d = *data++;
    /* cribbed from KG11-A */
        for (j = 0; j < 16; j++) {
             crc = (crc & ~01) | ((crc & 01) ^ (d & 01));
             crc = (crc & 01) ? (crc >> 1) ^ 0120001 : crc >> 1;
             d >>= 1;
        }
    }
    return (crc);
}

/* Unit service

   Note that for reads and writes, memory addresses wrap around in the
   current field.  This code assumes the entire disk is buffered.
*/

static t_stat rc_svc (UNIT *uptr)
{
    uint32      ma, da, t, u_old, u_new, last_da = 0;
    uint16      dat;
    uint16      *fbuf = (uint16 *) uptr->filebuf;

    if ((uptr->flags & UNIT_BUF) == 0) {                /* not buf? abort */
        update_rccs (RCCS_NED | RCCS_DONE, 0);          /* nx disk */
        return (IORETURN (rc_stopioe, SCPE_UNATT));
    }

    ma = GET_MEX (rc_cs) | rc_ca;                       /* 18b mem addr */
    da = rc_da * RC_NUMTR;                              /* sector->word offset */
    u_old = (da >> 16) & 03;                            /* save starting unit# */
    do {
        u_new = (da >> 16) & 03;
        if (u_new < u_old) {                            /* unit # overflow? */
            update_rccs (RCCS_NED, RCER_OVFL);
            break;
        }
        if (u_new >= UNIT_GETP(uptr->flags)) {          /* disk overflow? */
            update_rccs (RCCS_NED, 0);
            break;
        }
        if (uptr->FUNC == RFNC_READ) {                  /* read? */
            last_da = da & ~037;
            dat = fbuf[da];                             /* get disk data */
            rc_db = dat;
            if (Map_WriteW (ma, 2, &dat)) {             /* store mem, nxm? */
                update_rccs (0, RCER_NXM);
                break;
            }
        } else if (uptr->FUNC == RFNC_WCHK) {           /* write check? */
            last_da = da & ~037;
            rc_db = fbuf[da];                           /* get disk data */
            if (Map_ReadW (ma, 2, &dat)) {              /* read mem, nxm? */
                update_rccs (0, RCER_NXM);
                break;
            }
            if (rc_db != dat) {                         /* miscompare? */
                update_rccs (RCCS_WCHK, 0);
                break;
            }
        } else if (uptr->FUNC == RFNC_WRITE) {          /* write */
            t = (da >> 15) & 037;
            if (((rc_wlk >> t) & 1) ||
                 (uptr->flags & UNIT_RO)) {             /* write locked? */
                update_rccs (RCCS_WLK, 0);
                break;
            }
                                                        /* not locked */
            if (Map_ReadW (ma, 2, &dat)) {              /* read mem, nxm? */
                update_rccs (0, RCER_NXM);
                break;
            }
            fbuf[da] = dat;                             /* write word */
            rc_db = dat;
            if (da >= uptr->hwmark)
                uptr->hwmark = da + 1;
            } else {                                    /* look ahead */
            break;                                      /* no op for now */
        }
        rc_wc = (rc_wc + 1) & DMASK;                    /* incr word count */
        da = (da + 1) & 0777777;                        /* incr disk addr */
        if ((rc_cs & RCCS_INH) == 0)                    /* inhibit clear? */
        ma = (ma + 2) & UNIMASK;                        /* incr mem addr */
    } while (rc_wc != 0);                               /* brk if wc */
    rc_ca = ma & DMASK;                                 /* split ma */
    rc_cs = (rc_cs & ~RCCS_MEX) | ((ma >> (16 - RCCS_V_MEX)) & RCCS_MEX); 
    da += 31;
    rc_da = (da >> 5) & 017777;
    /* CRC of last 32W, if necessary */
    if ((uptr->FUNC == RFNC_READ) || (uptr->FUNC == RFNC_WCHK))
        rc_db = sectorCRC (&fbuf[last_da]);
    if (uptr->FUNC != RFNC_LAH)
        rc_la = rc_da;
    update_rccs (RCCS_DONE, 0);
    if (DEBUG_PRS (rc_dev))
        fprintf (sim_deb, ">>RC done: cs = %o, da = %o, ma = %o, wc = %o\n",
                 rc_cs, rc_da, rc_ca, rc_wc);
    return (SCPE_OK);
}

/* Update CS register */

static uint32 update_rccs (uint32 newcs, uint32 newer)
{
    uint32 oldcs = rc_cs;

    rc_er |= newer;                                     /* update RCER */
    rc_cs |= newcs;                                     /* update CS */
    if ((rc_cs & RCCS_ALLERR) || (rc_er != 0))          /* update CS<err> */
        rc_cs |= RCCS_ERR;
    else
        rc_cs &= ~RCCS_ERR;
    if ((rc_cs & RCCS_IE) &&                            /* IE and */
        (rc_cs & RCCS_DONE) && !(oldcs & RCCS_DONE))    /* done 0->1? */
        SET_INT (RC);
    return (rc_cs);
}

/* Reset routine */

static t_stat rc_reset (DEVICE *dptr)
{
    rc_cs = RCCS_DONE;
    rc_la = rc_da = 0;
    rc_er = 0;
    rc_wc = 0;
    rc_ca = 0;
    rc_maint = 0;
    rc_db = 0;
    CLR_INT (RC);
    sim_cancel (&rc_unit);
    return auto_config(0, 0);
}

/* Attach routine */

static t_stat rc_attach (UNIT *uptr, CONST char *cptr)
{
    uint32 sz, p;
    static const uint32 ds_bytes = RC_DKSIZE * sizeof (int16);

    if ((uptr->flags & UNIT_AUTO) && (sz = sim_fsize_name (cptr))) {
        p = (sz + ds_bytes - 1) / ds_bytes;
        if (p >= RC_NUMDK)
            p = RC_NUMDK - 1;
        uptr->flags = (uptr->flags & ~UNIT_PLAT) | (p << UNIT_V_PLAT);
    }
    uptr->capac = UNIT_GETP (uptr->flags) * RC_DKSIZE;
    return (attach_unit (uptr, cptr));
}

/* Change disk size */

static t_stat rc_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (val < 0)
        return (SCPE_IERR);
    if (uptr->flags & UNIT_ATT)
        return (SCPE_ALATT);
    uptr->capac = UNIT_GETP (val) * RC_DKSIZE;
    uptr->flags = uptr->flags & ~UNIT_AUTO;
    return (SCPE_OK);
}

static const char *rc_description (DEVICE *dptr)
{
    return "RC11/RS64 fixed head disk controller";
}
