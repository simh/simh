/* pdp8_rl.c: RL8A cartridge disk simulator

   Copyright (c) 1993-2013, Robert M Supnik

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

   rl           RL8A cartridge disk

   17-Sep-13    RMS     Changed to use central set_bootpc routine
   25-Oct-05    RMS     Fixed IOT 61 decode bug (David Gesswein)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   04-Jan-04    RMS     Changed attach routine to use sim_fsize
   25-Apr-03    RMS     Revised for extended file support
   04-Oct-02    RMS     Added DIB, device number support
   06-Jan-02    RMS     Changed enable/disable support
   30-Nov-01    RMS     Cloned from RL11

   The RL8A is a four drive cartridge disk subsystem.  An RL01 drive
   consists of 256 cylinders, each with 2 surfaces containing 40 sectors
   of 256 bytes.  An RL02 drive has 512 cylinders.

   The RL8A controller has several serious complications.
   - Seeking is relative to the current disk address; this requires
     keeping accurate track of the current cylinder.
   - The RL8A will not switch heads or cross cylinders during transfers.
   - The RL8A operates in 8b and 12b mode, like the RX8E; in 12b mode, it
     packs 2 12b words into 3 bytes, creating a 170 "word" sector with
     one wasted byte.  Multi-sector transfers in 12b mode don't work.
*/

#include "pdp8_defs.h"

/* Constants */

#define RL_NUMBY        256                             /* 8b bytes/sector */
#define RL_NUMSC        40                              /* sectors/surface */
#define RL_NUMSF        2                               /* surfaces/cylinder */
#define RL_NUMCY        256                             /* cylinders/drive */
#define RL_NUMDR        4                               /* drives/controller */
#define RL_MAXFR        (1 << 12)                       /* max transfer */
#define RL01_SIZE       (RL_NUMCY*RL_NUMSF*RL_NUMSC*RL_NUMBY)  /* words/drive */
#define RL02_SIZE       (RL01_SIZE * 2)                 /* words/drive */
#define RL_BBMAP        014                             /* sector for bblk map */
#define RL_BBID         0123                            /* ID for bblk map */

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write lock */
#define UNIT_V_RL02     (UNIT_V_UF + 1)                 /* RL01 vs RL02 */
#define UNIT_V_AUTO     (UNIT_V_UF + 2)                 /* autosize enable */
#define UNIT_V_DUMMY    (UNIT_V_UF + 3)                 /* dummy flag */
#define UNIT_DUMMY      (1u << UNIT_V_DUMMY)
#define UNIT_WLK        (1u << UNIT_V_WLK)
#define UNIT_RL02       (1u << UNIT_V_RL02)
#define UNIT_AUTO       (1u << UNIT_V_AUTO)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

/* Parameters in the unit descriptor */

#define TRK             u3                              /* current cylinder */
#define STAT            u4                              /* status */

/* RLDS, NI = not implemented, * = kept in STAT, ^ = kept in TRK */

#define RLDS_LOAD       0                               /* no cartridge */
#define RLDS_LOCK       5                               /* lock on */
#define RLDS_BHO        0000010                         /* brushes home NI */
#define RLDS_HDO        0000020                         /* heads out NI */
#define RLDS_CVO        0000040                         /* cover open NI */
#define RLDS_HD         0000100                         /* head select ^ */
#define RLDS_RL02       0000200                         /* RL02 */
#define RLDS_DSE        0000400                         /* drv sel err NI */
#define RLDS_VCK        0001000                         /* vol check * */
#define RLDS_WGE        0002000                         /* wr gate err * */
#define RLDS_SPE        0004000                         /* spin err * */
#define RLDS_STO        0010000                         /* seek time out NI */
#define RLDS_WLK        0020000                         /* wr locked */
#define RLDS_HCE        0040000                         /* hd curr err NI */
#define RLDS_WDE        0100000                         /* wr data err NI */
#define RLDS_ATT        (RLDS_HDO+RLDS_BHO+RLDS_LOCK)   /* att status */
#define RLDS_UNATT      (RLDS_CVO+RLDS_LOAD)            /* unatt status */
#define RLDS_ERR        (RLDS_WDE+RLDS_HCE+RLDS_STO+RLDS_SPE+RLDS_WGE+ \
                         RLDS_VCK+RLDS_DSE)             /* errors bits */

/* RLCSA, seek = offset/rw = address (also uptr->TRK) */

#define RLCSA_DIR       04000                           /* direction */
#define RLCSA_HD        02000                           /* head select */
#define RLCSA_CYL       00777                           /* cyl offset */
#define GET_CYL(x)      ((x) & RLCSA_CYL)
#define GET_TRK(x)      ((((x) & RLCSA_CYL) * RL_NUMSF) + \
                        (((x) & RLCSA_HD)? 1: 0))
#define GET_DA(x)       ((GET_TRK(x) * RL_NUMSC) + rlsa)

/* RLCSB, function/unit select */

#define RLCSB_V_FUNC    0                               /* function */
#define RLCSB_M_FUNC    07
#define  RLCSB_MNT      0
#define  RLCSB_CLRD     1
#define  RLCSB_GSTA     2
#define  RLCSB_SEEK     3
#define  RLCSB_RHDR     4
#define  RLCSB_WRITE    5
#define  RLCSB_READ     6
#define  RLCSB_RNOHDR   7
#define RLCSB_V_MEX     3                               /* memory extension */
#define RLCSB_M_MEX     07
#define RLCSB_V_DRIVE   6                               /* drive */
#define RLCSB_M_DRIVE   03
#define RLCSB_V_IE      8                               /* int enable */
#define RLCSB_IE        (1u << RLCSB_V_IE)
#define RLCSB_8B        01000                           /* 12b/8b */
#define RCLS_MNT        02000                           /* maint NI */
#define RLCSB_RW        0001777                         /* read/write */
#define GET_FUNC(x)     (((x) >> RLCSB_V_FUNC) & RLCSB_M_FUNC)
#define GET_MEX(x)      (((x) >> RLCSB_V_MEX) & RLCSB_M_MEX)
#define GET_DRIVE(x)    (((x) >> RLCSB_V_DRIVE) & RLCSB_M_DRIVE)

/* RLSA, disk sector */

#define RLSA_V_SECT     6                               /* sector */
#define RLSA_M_SECT     077
#define GET_SECT(x)     (((x) >> RLSA_V_SECT) & RLSA_M_SECT)

/* RLER, error register */

#define RLER_DRDY       00001                           /* drive ready */
#define RLER_DRE        00002                           /* drive error */
#define RLER_HDE        01000                           /* header error */
#define RLER_INCMP      02000                           /* incomplete */
#define RLER_ICRC       04000                           /* CRC error */
#define RLER_MASK       07003

/* RLSI, silo register, used only in read header */

#define RLSI_V_TRK      6                               /* track */

extern uint16 M[];
extern int32 int_req;
extern UNIT cpu_unit;

uint8 *rlxb = NULL;                                     /* xfer buffer */
int32 rlcsa = 0;                                        /* control/status A */
int32 rlcsb = 0;                                        /* control/status B */
int32 rlma = 0;                                         /* memory address */
int32 rlwc = 0;                                         /* word count */
int32 rlsa = 0;                                         /* sector address */
int32 rler = 0;                                         /* error register */
int32 rlsi = 0, rlsi1 = 0, rlsi2 = 0;                   /* silo queue */
int32 rl_lft = 0;                                       /* silo left/right */
int32 rl_done = 0;                                      /* done flag */
int32 rl_erf = 0;                                       /* error flag */
int32 rl_swait = 10;                                    /* seek wait */
int32 rl_rwait = 10;                                    /* rotate wait */
int32 rl_stopioe = 1;                                   /* stop on error */

DEVICE rl_dev;
int32 rl60 (int32 IR, int32 AC);
int32 rl61 (int32 IR, int32 AC);
t_stat rl_svc (UNIT *uptr);
t_stat rl_reset (DEVICE *dptr);
void rl_set_done (int32 error);
t_stat rl_boot (int32 unitno, DEVICE *dptr);
t_stat rl_attach (UNIT *uptr, char *cptr);
t_stat rl_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat rl_set_bad (UNIT *uptr, int32 val, char *cptr, void *desc);

/* RL8A data structures

   rl_dev       RL device descriptor
   rl_unit      RL unit list
   rl_reg       RL register list
   rl_mod       RL modifier list
*/

DIB rl_dib = { DEV_RL, 2, { &rl60, &rl61 } };

UNIT rl_unit[] = {
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE, RL01_SIZE) },
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE, RL01_SIZE) },
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE, RL01_SIZE) },
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE, RL01_SIZE) }
    };

REG rl_reg[] = {
    { ORDATA (RLCSA, rlcsa, 12) },
    { ORDATA (RLCSB, rlcsb, 12) },
    { ORDATA (RLMA, rlma, 12) },
    { ORDATA (RLWC, rlwc, 12) },
    { ORDATA (RLSA, rlsa, 6) },
    { ORDATA (RLER, rler, 12) },
    { ORDATA (RLSI, rlsi, 16) },
    { ORDATA (RLSI1, rlsi1, 16) },
    { ORDATA (RLSI2, rlsi2, 16) },
    { FLDATA (RLSIL, rl_lft, 0) },
    { FLDATA (INT, int_req, INT_V_RL) },
    { FLDATA (DONE, rl_done, INT_V_RL) },
    { FLDATA (IE, rlcsb, RLCSB_V_IE) },
    { FLDATA (ERR, rl_erf, 0) },
    { DRDATA (STIME, rl_swait, 24), PV_LEFT },
    { DRDATA (RTIME, rl_rwait, 24), PV_LEFT },
    { URDATA (CAPAC, rl_unit[0].capac, 10, T_ADDR_W, 0,
              RL_NUMDR, PV_LEFT + REG_HRO) },
    { FLDATA (STOP_IOE, rl_stopioe, 0) },
    { ORDATA (DEVNUM, rl_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB rl_mod[] = {
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { UNIT_DUMMY, 0, NULL, "BADBLOCK", &rl_set_bad },
    { (UNIT_RL02+UNIT_ATT), UNIT_ATT, "RL01", NULL, NULL },
    { (UNIT_RL02+UNIT_ATT), (UNIT_RL02+UNIT_ATT), "RL02", NULL, NULL },
    { (UNIT_AUTO+UNIT_RL02+UNIT_ATT), 0, "RL01", NULL, NULL },
    { (UNIT_AUTO+UNIT_RL02+UNIT_ATT), UNIT_RL02, "RL02", NULL, NULL },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
    { UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL },
    { (UNIT_AUTO+UNIT_RL02), 0, NULL, "RL01", &rl_set_size },
    { (UNIT_AUTO+UNIT_RL02), UNIT_RL02, NULL, "RL02", &rl_set_size },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE rl_dev = {
    "RL", rl_unit, rl_reg, rl_mod,
    RL_NUMDR, 8, 24, 1, 8, 8,
    NULL, NULL, &rl_reset,
    &rl_boot, &rl_attach, NULL,
    &rl_dib, DEV_DISABLE | DEV_DIS
    };

/* IOT routines */

int32 rl60 (int32 IR, int32 AC)
{
int32 curr, offs, newc, maxc;
UNIT *uptr;

switch (IR & 07) {                                      /* case IR<9:11> */

    case 0:                                             /* RLDC */
        rl_reset (&rl_dev);                             /* reset device */
        break;

    case 1:                                             /* RLSD */
        if (rl_done)                                    /* skip if done */
            AC = IOT_SKP;
        else AC = 0;
        rl_done = 0;                                    /* clear done */
        int_req = int_req & ~INT_RL;                    /* clear intr */
        return AC;

    case 2:                                             /* RLMA */
        rlma = AC;
        break;

    case 3:                                             /* RLCA */
        rlcsa = AC;
        break;

    case 4:                                             /* RLCB */
        rlcsb = AC;
        rl_done = 0;                                    /* clear done */
        rler = rl_erf = 0;                              /* clear errors */
        int_req = int_req & ~INT_RL;                    /* clear intr */
        rl_lft = 0;                                     /* clear silo ptr */
        uptr = rl_dev.units + GET_DRIVE (rlcsb);        /* select unit */
        switch (GET_FUNC (rlcsb)) {                     /* case on func */

        case RLCSB_CLRD:                                /* clear drive */
            uptr->STAT = uptr->STAT & ~RLDS_ERR;        /* clear errors */
        case RLCSB_MNT:                                 /* mnt */
            rl_set_done (0);
            break;

        case RLCSB_SEEK:                                /* seek */
            curr = GET_CYL (uptr->TRK);                 /* current cylinder */
            offs = GET_CYL (rlcsa);                     /* offset */
            if (rlcsa & RLCSA_DIR) {                    /* in or out? */
                newc = curr + offs;                     /* out */
                maxc = (uptr->flags & UNIT_RL02)?
                        RL_NUMCY * 2: RL_NUMCY;
                if (newc >= maxc) newc = maxc - 1;
                }
            else {
                newc = curr - offs;                     /* in */
                if (newc < 0) newc = 0;
                }
            uptr->TRK = newc | (rlcsa & RLCSA_HD);
            sim_activate (uptr, rl_swait * abs (newc - curr));
            break;

        default:                                        /* data transfer */
            sim_activate (uptr, rl_swait);              /* activate unit */
            break;
            }                                           /* end switch func */
        break;

    case 5:                                             /* RLSA */
        rlsa = GET_SECT (AC);
        break;

    case 6:                                             /* spare */
        return 0;

    case 7:                                             /* RLWC */
        rlwc = AC;
        break;
        }                                               /* end switch pulse */

return 0;                                               /* clear AC */
}

int32 rl61 (int32 IR, int32 AC)
{
int32 dat;
UNIT *uptr;

switch (IR & 07) {                                      /* case IR<9:11> */

    case 0:                                             /* RRER */
        uptr = rl_dev.units + GET_DRIVE (rlcsb);        /* select unit */
        if (!sim_is_active (uptr) &&                    /* update drdy */
            (uptr->flags & UNIT_ATT))
            rler = rler | RLER_DRDY;
        else rler = rler & ~RLER_DRDY;
        dat = rler & RLER_MASK;
        break;

    case 1:                                             /* RRWC */
        dat = rlwc;
        break;

    case 2:                                             /* RRCA */
        dat = rlcsa;
        break;

    case 3:                                             /* RRCB */
        dat = rlcsb;
        break;

    case 4:                                             /* RRSA */
        dat = (rlsa << RLSA_V_SECT) & 07777;
        break;

    case 5:                                             /* RRSI */
        if (rl_lft) {                                   /* silo left? */
            dat = (rlsi >> 8) & 0377;                   /* get left 8b */
            rlsi = rlsi1;                               /* ripple */
            rlsi1 = rlsi2;
            }
        else dat = rlsi & 0377;                         /* get right 8b */
        rl_lft = rl_lft ^ 1;                            /* change side */
        break;

    case 6:                                             /* spare */
        return AC;

    case 7:                                             /* RLSE */
        if (rl_erf)                                     /* skip if err */
            dat = IOT_SKP | AC;
        else dat = AC;
        rl_erf = 0;
        break;
        }                                               /* end switch pulse */

return dat;
}

/* Service unit timeout

   If seek in progress, complete seek command
   Else complete data transfer command

   The unit control block contains the function and cylinder for
   the current command.
*/

t_stat rl_svc (UNIT *uptr)
{
int32 err, wc, maxc;
int32 i, j, func, da, bc, wbc;
uint32 ma;

func = GET_FUNC (rlcsb);                                /* get function */
if (func == RLCSB_GSTA) {                               /* get status? */
    rlsi = uptr->STAT | 
        ((uptr->TRK & RLCSA_HD)? RLDS_HD: 0) |
        ((uptr->flags & UNIT_ATT)? RLDS_ATT: RLDS_UNATT);
    if (uptr->flags & UNIT_RL02)
        rlsi = rlsi | RLDS_RL02;
    if (uptr->flags & UNIT_WPRT)
        rlsi = rlsi | RLDS_WLK;
    rlsi2 = rlsi1 = rlsi;
    rl_set_done (0);                                    /* done */
    return SCPE_OK;
    }

if ((uptr->flags & UNIT_ATT) == 0) {                    /* attached? */
    uptr->STAT = uptr->STAT | RLDS_SPE;                 /* spin error */
    rl_set_done (RLER_INCMP);                           /* flag error */
    return IORETURN (rl_stopioe, SCPE_UNATT);
    }

if ((func == RLCSB_WRITE) && (uptr->flags & UNIT_WPRT)) {
    uptr->STAT = uptr->STAT | RLDS_WGE;                 /* write and locked */
    rl_set_done (RLER_DRE);                             /* flag error */
    return SCPE_OK;
    }

if (func == RLCSB_SEEK) {                               /* seek? */
    rl_set_done (0);                                    /* done */
    return SCPE_OK;
    }

if (func == RLCSB_RHDR) {                               /* read header? */
    rlsi = (GET_TRK (uptr->TRK) << RLSI_V_TRK) | rlsa;
    rlsi1 = rlsi2 = 0;
    rl_set_done (0);                                    /* done */
    return SCPE_OK;
    }

if (((func != RLCSB_RNOHDR) && (GET_CYL (uptr->TRK) != GET_CYL (rlcsa)))
   || (rlsa >= RL_NUMSC)) {                             /* bad cyl or sector? */
    rl_set_done (RLER_HDE | RLER_INCMP);                /* flag error */
    return SCPE_OK;
    }
    
ma = (GET_MEX (rlcsb) << 12) | rlma;                    /* get mem addr */
da = GET_DA (rlcsa) * RL_NUMBY;                         /* get disk addr */
wc = 010000 - rlwc;                                     /* get true wc */
if (rlcsb & RLCSB_8B) {                                 /* 8b mode? */
    bc = wc;                                            /* bytes to xfr */
    maxc = (RL_NUMSC - rlsa) * RL_NUMBY;                /* max transfer */
    if (bc > maxc)                                      /* trk ovrun? limit */
        wc = bc = maxc;
    }
else {
    bc = ((wc * 3) + 1) / 2;                            /* 12b mode */
    if (bc > RL_NUMBY) {                                /* > 1 sector */
        bc = RL_NUMBY;                                  /* cap xfer */
        wc = (RL_NUMBY * 2) / 3;
        }
    }

err = fseek (uptr->fileref, da, SEEK_SET);

if ((func >= RLCSB_READ) && (err == 0) &&               /* read (no hdr)? */
    MEM_ADDR_OK (ma)) {                                 /* valid bank? */
    i = fxread (rlxb, sizeof (int8), bc, uptr->fileref);
    err = ferror (uptr->fileref);
    for ( ; i < bc; i++)                                /* fill buffer */
        rlxb[i] = 0;
    for (i = j = 0; i < wc; i++) {                      /* store buffer */
        if (rlcsb & RLCSB_8B)                           /* 8b mode? */
            M[ma] = rlxb[i] & 0377;                     /* store */
        else if (i & 1) {                               /* odd wd 12b? */
            M[ma] = ((rlxb[j + 1] >> 4) & 017) |
                (((uint16) rlxb[j + 2]) << 4);
            j = j + 3;
            }
        else M[ma] = rlxb[j] |                          /* even wd 12b */
            ((((uint16) rlxb[j + 1]) & 017) << 8);      
        ma = (ma & 070000) + ((ma + 1) & 07777);
        }                                               /* end for */
    }                                                   /* end if wr */

if ((func == RLCSB_WRITE) && (err == 0)) {              /* write? */
    for (i = j = 0; i < wc; i++) {                      /* fetch buffer */
        if (rlcsb & RLCSB_8B)                           /* 8b mode? */
            rlxb[i] = M[ma] & 0377;                     /* fetch */
        else if (i & 1) {                               /* odd wd 12b? */
            rlxb[j + 1] = rlxb[j + 1] | ((M[ma] & 017) << 4);
            rlxb[j + 2] = ((M[ma] >> 4) & 0377);
            j = j + 3;
            }
        else {                                          /* even wd 12b */
            rlxb[j] = M[ma] & 0377;
            rlxb[j + 1] = (M[ma] >> 8) & 017;
            }
        ma = (ma & 070000) + ((ma + 1) & 07777);
        }                                               /* end for */
    wbc = (bc + (RL_NUMBY - 1)) & ~(RL_NUMBY - 1);      /* clr to */
    for (i = bc; i < wbc; i++)                          /* end of blk */
        rlxb[i] = 0;
    fxwrite (rlxb, sizeof (int8), wbc, uptr->fileref);
    err = ferror (uptr->fileref);
    }                                                   /* end write */

rlwc = (rlwc + wc) & 07777;                             /* final word count */
if (rlwc != 0)                                          /* completed? */
    rler = rler | RLER_INCMP;
rlma = (rlma + wc) & 07777;                             /* final word addr */
rlsa = rlsa + ((bc + (RL_NUMBY - 1)) / RL_NUMBY);
rl_set_done (0);

if (err != 0) {                                         /* error? */
    perror ("RL I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Set done and possibly errors */

void rl_set_done (int32 status)
{
rl_done = 1;
rler = rler | status;
if (rler)
    rl_erf = 1;
if (rlcsb & RLCSB_IE)
    int_req = int_req | INT_RL;
else int_req = int_req & ~INT_RL;
return;
}

/* Device reset

   Note that the RL8A does NOT recalibrate its drives on RESET
*/

t_stat rl_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

rlcsa = rlcsb = rlsa = rler = 0;
rlma = rlwc = 0;
rlsi = rlsi1 = rlsi2 = 0;
rl_lft = 0;
rl_done = 0;
rl_erf = 0;
int_req = int_req & ~INT_RL;
for (i = 0; i < RL_NUMDR; i++) {
    uptr = rl_dev.units + i;
    sim_cancel (uptr);
    uptr->STAT = 0;
    }
if (rlxb == NULL)
    rlxb = (uint8 *) calloc (RL_MAXFR, sizeof (uint8));
if (rlxb == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

/* Attach routine */

t_stat rl_attach (UNIT *uptr, char *cptr)
{
uint32 p;
t_stat r;

uptr->capac = (uptr->flags & UNIT_RL02)? RL02_SIZE: RL01_SIZE;
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
uptr->TRK = 0;                                          /* cyl 0 */
uptr->STAT = RLDS_VCK;                                  /* new volume */
if ((p = sim_fsize (uptr->fileref)) == 0) {             /* new disk image? */
    if (uptr->flags & UNIT_RO)
        return SCPE_OK;
    return rl_set_bad (uptr, 0, NULL, NULL);
    }
if ((uptr->flags & UNIT_AUTO) == 0)                     /* autosize? */
    return r;
if (p > (RL01_SIZE * sizeof (int16))) {
    uptr->flags = uptr->flags | UNIT_RL02;
    uptr->capac = RL02_SIZE;
    }
else {
    uptr->flags = uptr->flags & ~UNIT_RL02;
    uptr->capac = RL01_SIZE;
    }
return SCPE_OK;
}

/* Set size routine */

t_stat rl_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = (val & UNIT_RL02)? RL02_SIZE: RL01_SIZE;
return SCPE_OK;
}

/* Factory bad block table creation routine

   This routine writes the OS/8 specific bad block map in track 0, sector 014 (RL_BBMAP):

        words 0 magic number = 0123 (RL_BBID)
        words 1-n       block numbers
         :
        words n+1       end of table = 0

   Inputs:
        uptr    =       pointer to unit
        val     =       ignored
   Outputs:
        sta     =       status code
*/

t_stat rl_set_bad (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 i, da = RL_BBMAP * RL_NUMBY;

if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_UNATT;
if (uptr->flags & UNIT_RO)
    return SCPE_RO;
if (!get_yn ("Create bad block table? [N]", FALSE))
    return SCPE_OK;
if (fseek (uptr->fileref, da, SEEK_SET))
    return SCPE_IOERR;
rlxb[0] = RL_BBID;
for (i = 1; i < RL_NUMBY; i++)
    rlxb[i] = 0;
fxwrite (rlxb, sizeof (uint8), RL_NUMBY, uptr->fileref);
if (ferror (uptr->fileref))
    return SCPE_IOERR;
return SCPE_OK;
}

/* Bootstrap */

#define BOOT_START 1                                    /* start */
#define BOOT_UNIT 02006                                 /* unit number */
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    06600,                      /* BT, RLDC             ; reset */
    07201,                      /* 02, CLA IAC          ; clr drv = 1 */
    04027,                      /* 03, JMS GO           ; do io */
    01004,                      /* 04, TAD 4            ; rd hdr fnc */
    04027,                      /* 05, JMS GO           ; do io */
    06615,                      /* 06, RRSI             ; rd hdr lo */
    07002,                      /* 07, BSW              ; swap */
    07012,                      /* 10, RTR              ; lo cyl to L */
    06615,                      /* 11, RRSI             ; rd hdr hi */
    00025,                      /* 12, AND 25           ; mask = 377 */
    07004,                      /* 13, RTL              ; get cyl */
    06603,                      /* 14, RLCA             ; set addr */
    07325,                      /* 15, CLA STL IAC RAL  ; seek = 3 */
    04027,                      /* 16, JMS GO           ; do io */
    07332,                      /* 17, CLA STL RTR      ; dir in = 2000 */
    06605,                      /* 20, RLSA             ; sector */             
    01026,                      /* 21, TAD (-200)       ; one sector */
    06607,                      /* 22, RLWC             ; word cnt */
    07327,                      /* 23, CLA STL IAC RTL  ; read = 6*/
    04027,                      /* 24, JMS GO           ; do io */
    00377,                      /* 25, JMP 377          ; start */
    07600,                      /* 26, -200             ; word cnt */
    00000,                      /* GO, 0                ; subr */
    06604,                      /* 30, RLCB             ; load fnc */
    06601,                      /* 31, RLSD             ; wait */
    05031,                      /* 32, JMP .-1          ; */
    06617,                      /* 33, RLSE             ; error? */
    05427,                      /* 34, JMP I GO         ; no, ok */
    05001                       /* 35, JMP BT           ; restart */
    };


t_stat rl_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

if (unitno)                                             /* only unit 0 */
    return SCPE_ARG;
if (rl_dib.dev != DEV_RL)                               /* only std devno */
    return STOP_NOTSTD;
rl_unit[unitno].TRK = 0;
for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = boot_rom[i];
cpu_set_bootpc (BOOT_START);
return SCPE_OK;
}
