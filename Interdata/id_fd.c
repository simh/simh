/* id_fd.c: Interdata floppy disk simulator

   Copyright (c) 2001-2013, Robert M Supnik

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

   fd           M46-630 floppy disk

   03-Sep-13    RMS     Added explicit void * cast
   19-Mar-12    RMS     Fixed macro naming conflict (Mark Pizzolato)

   A diskette consists of 77 tracks, each with 26 sectors of 128B.  The
   Interdata floppy uses a logical record numbering scheme from 1 to 2002.
   Physical tracks are numbered 0-76, physical sectors 1-26.

   To allow for deleted data handling, a directory is appended to the end
   of the image, one byte per LRN.  Zero (the default) is a normal record,
   non-zero a deleted record.
*/

#include "id_defs.h"

#define FD_NUMTR        77                              /* tracks/disk */
#define FD_NUMSC        26                              /* sectors/track */
#define FD_NUMBY        128                             /* bytes/sector */
#define FD_NUMLRN       (FD_NUMTR * FD_NUMSC)           /* LRNs/disk */
#define FD_SIZE         (FD_NUMLRN * FD_NUMBY)          /* bytes/disk */
#define FD_NUMDR        4                               /* drives/controller */
#define UNIT_V_WLK      (UNIT_V_UF)                     /* write locked */
#define UNIT_WLK        (1u << UNIT_V_UF)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */
#define LRN             u3                              /* last LRN */
#define FNC             u4                              /* last function */
#define GET_DA(x)       (((x) - 1) * FD_NUMBY)
#define GET_TRK(x)      (((x) - 1) / FD_NUMSC)
#define GET_SEC(x)      ((((x) - 1) % FD_NUMSC) + 1)
#define LRN_BOOT        5                               /* boot block LRN */

/* Command byte */

#define CMD_V_UNIT      4                               /* unit */
#define CMD_M_UNIT      0x3
#define GET_UNIT(x)     (((x) >> CMD_V_UNIT) & CMD_M_UNIT)
#define CMD_V_FNC       0                               /* function */
#define CMD_M_FNC       0xF
#define GET_FNC(x)      (((x) >> CMD_V_FNC) & CMD_M_FNC)
#define  FNC_RD         0x1                             /* read */
#define  FNC_WR         0x2                             /* write */
#define  FNC_RDID       0x3                             /* read ID */
#define  FNC_RSTA       0x4                             /* read status */
#define  FNC_DEL        0x5                             /* write deleted */
#define  FNC_BOOT       0x6                             /* boot */
#define  FNC_STOP       0x7                             /* stop */
#define  FNC_RESET      0x8                             /* reset */
#define  FNC_FMT        0x9                             /* format NI */
#define FNC_STOPPING    0x10                            /* stopping */

/* Status byte, * = dynamic */

#define STA_WRP         0x80                            /* *write prot */
#define STA_DEF         0x40                            /* def track NI */
#define STA_DLR         0x20                            /* del record */
#define STA_ERR         0x10                            /* error */
#define STA_IDL         0x02                            /* idle */
#define STA_OFL         0x01                            /* fault */
#define STA_MASK        (STA_DEF|STA_DLR|STA_ERR|STA_BSY|STA_IDL)
#define SET_EX          (STA_ERR)                       /* set EX */

/* Extended status, 6 bytes, * = dynamic */

#define ES_SIZE         6
#define ES0_HCRC        0x80                            /* ID CRC NI */
#define ES0_DCRC        0x40                            /* data CRC NI */
#define ES0_LRN         0x20                            /* illegal LRN */
#define ES0_WRP         0x10                            /* *write prot */
#define ES0_ERR         0x08                            /* error */
#define ES0_DEF         0x04                            /* def trk NI */
#define ES0_DEL         0x02                            /* del rec NI */
#define ES0_FLT         0x01                            /* fault */
#define ES1_TK0         0x80                            /* track 0 */
#define ES1_NRDY        0x40                            /* not ready */
#define ES1_NOAM        0x20                            /* no addr mk NI */
#define ES1_CMD         0x10                            /* illegal cmd */
#define ES1_SKE         0x08                            /* seek err NI */
#define ES1_UNS         0x04                            /* unsafe NI */
#define ES1_UNIT        0x03                            /* unit # */

/* Processing options for commands */

#define C_RD            0x1                             /* cmd reads disk */
#define C_WD            0x2                             /* cmd writes disk */

extern uint32 int_req[INTSZ], int_enb[INTSZ];

uint32 fd_sta = 0;                                      /* status */
uint32 fd_cmd = 0;                                      /* command */
uint32 fd_db = 0;                                       /* data buffer */
uint32 fd_bptr = 0;                                     /* buffer pointer */
uint8 fdxb[FD_NUMBY] = { 0 };                           /* sector buffer */
uint8 fd_es[FD_NUMDR][ES_SIZE] = { {0} };               /* ext status */
uint32 fd_lrn = 0;                                      /* log rec # */
uint32 fd_wdv = 0;                                      /* wd valid */
uint32 fd_stopioe = 1;                                  /* stop on error */
uint32 fd_arm = 0;                                      /* intr arm */
int32 fd_ctime = 100;                                   /* command time */
int32 fd_stime = 10;                                    /* seek, per LRN */
int32 fd_xtime = 1;                                     /* tr set time */

static uint32 ctab[16] = {
    0, C_RD, C_WD, 0,                                   /* 0, rd, wr, 0 */
    0, C_WD, C_RD, 0,                                   /* 0, del, boot, 0 */
    0, 0, 0, 0,
    0, 0, 0, 0
    };

DEVICE fd_dev;
uint32 fd (uint32 dev, uint32 op, uint32 dat);
t_stat fd_svc (UNIT *uptr);
t_stat fd_reset (DEVICE *dptr);
t_stat fd_clr (DEVICE *dptr);
t_stat fd_boot (int32 unitno, DEVICE *dptr);
t_bool fd_dte (UNIT *uptr, t_bool wr);
uint32 fd_crc (uint32 crc, uint32 dat, uint32 cnt);
void fd_done (uint32 u, uint32 nsta, uint32 nes0, uint32 nes1);
void sched_seek (UNIT *uptr, int32 newlrn);

/* FD data structures

   fd_dev       FD device descriptor
   fd_unit      FD unit list
   fd_reg       FD register list
   fd_mod       FD modifier list
*/

DIB fd_dib = { d_FD, -1, v_FD, NULL, &fd, NULL };

UNIT fd_unit[] = {
    { UDATA (&fd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_BUFABLE+UNIT_MUSTBUF, FD_SIZE + FD_NUMLRN) },
    { UDATA (&fd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_BUFABLE+UNIT_MUSTBUF, FD_SIZE + FD_NUMLRN) },
    { UDATA (&fd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_BUFABLE+UNIT_MUSTBUF, FD_SIZE + FD_NUMLRN) },
    { UDATA (&fd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_BUFABLE+UNIT_MUSTBUF, FD_SIZE + FD_NUMLRN) }
    };

REG fd_reg[] = {
    { HRDATA (CMD, fd_cmd, 8) },
    { HRDATA (STA, fd_sta, 8) },
    { HRDATA (BUF, fd_db, 8) },
    { HRDATA (LRN, fd_lrn, 16) },
    { BRDATA (ESTA, fd_es, 16, 8, ES_SIZE * FD_NUMDR) },
    { BRDATA (DBUF, fdxb, 16, 8, FD_NUMBY) },
    { HRDATA (DBPTR, fd_bptr, 8) },
    { FLDATA (WDV, fd_wdv, 0) },
    { FLDATA (IREQ, int_req[l_FD], i_FD) },
    { FLDATA (IENB, int_enb[l_FD], i_FD) },
    { FLDATA (IARM, fd_arm, 0) },
    { DRDATA (CTIME, fd_ctime, 24), PV_LEFT },
    { DRDATA (STIME, fd_stime, 24), PV_LEFT },
    { DRDATA (XTIME, fd_xtime, 24), PV_LEFT },
    { FLDATA (STOP_IOE, fd_stopioe, 0) },
    { URDATA (ULRN, fd_unit[0].LRN, 16, 16, 0, FD_NUMDR, REG_HRO) },
    { URDATA (UFNC, fd_unit[0].FNC, 16, 8, 0, FD_NUMDR, REG_HRO) },
    { HRDATA (DEVNO, fd_dib.dno, 8), REG_HRO },
    { NULL }
    };

MTAB fd_mod[] = {
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE fd_dev = {
    "FD", fd_unit, fd_reg, fd_mod,
    FD_NUMDR, 16, 20, 1, 16, 8,
    NULL, NULL, &fd_reset,
    &fd_boot, NULL, NULL,
    &fd_dib, DEV_DISABLE
    };

/* Floppy disk: IO routine */

uint32 fd (uint32 dev, uint32 op, uint32 dat)
{
int32 u, t, fnc;
UNIT *uptr;

fnc = GET_FNC (fd_cmd);                                 /* get fnc */
u = GET_UNIT (fd_cmd);                                  /* get unit */
uptr = fd_dev.units + u;
switch (op) {                                           /* case IO op */
    case IO_ADR:                                        /* select */
        return BY;                                      /* byte only */

    case IO_RD:                                         /* read */
        if (fd_sta & (STA_IDL | STA_BSY))               /* idle, busy? */
            return fd_db;
        if (fd_bptr < FD_NUMBY)                         /* get byte */
            fd_db = fdxb[fd_bptr++];
        if (fd_bptr >= FD_NUMBY) {                      /* buf end? */
            if (ctab[fnc] & C_RD) {                     /* disk read? */
                sched_seek (uptr, uptr->LRN + 1);       /* sched read */
                fd_sta = fd_sta | STA_BSY;              /* set busy */
                }
            else fd_bptr = 0;                           /* just wrap */
			}
        if ((ctab[fnc] & C_RD) && fd_arm)               /* if rd & arm, */
            SET_INT (v_FD);                             /* interrupt */
        return fd_db;                                   /* return buf */

    case IO_WD:                                         /* write */
        if (fd_sta & STA_IDL) {                         /* idle? */
            fd_lrn = ((fd_lrn << 8) | dat) & DMASK16;   /* insert byte */
            fd_wdv = 1;
            break;
            }
        if (fd_bptr < FD_NUMBY)                         /* if room, */
            fdxb[fd_bptr++] = fd_db = dat;              /* store byte */
        if (fd_bptr >= FD_NUMBY) {                      /* buf end? */
            if (ctab[fnc] & C_WD) {                     /* disk write? */
                sched_seek (uptr, uptr->LRN + 1);       /* sched write */
                fd_sta = fd_sta | STA_BSY;              /* set busy */
                }
            else fd_bptr = 0;                           /* just wrap */
            }
        if ((ctab[fnc] & C_WD) && fd_arm)               /* if wr & arm, */
            SET_INT (v_FD);                             /* interrupt */
        break;

    case IO_SS:                                         /* status */
        t = fd_sta & STA_MASK;                          /* get status */
        if ((uptr->flags & UNIT_ATT) == 0)
            t = t | STA_DU;
        if (t & SET_EX)                                 /* test for ex */
            t = t | STA_EX;
        return t;

    case IO_OC:                                         /* command */
        fd_arm = int_chg (v_FD, dat, fd_arm);           /* upd int ctrl */
        fnc = GET_FNC (dat);                            /* new fnc */
        fd_cmd = dat;                                   /* save cmd */
        u = GET_UNIT (dat);                             /* get unit */
        uptr = fd_dev.units + u;
        if (fnc == FNC_STOP) {                          /* stop? */
            uptr->FNC = uptr->FNC | FNC_STOPPING;       /* flag stop */
            if (sim_is_active (uptr))                   /* busy? cont */
                break;
            if (ctab[GET_FNC (uptr->FNC)] & C_WD) {     /* write? */
                sched_seek (uptr, uptr->LRN + 1);       /* sched write */
                fd_sta = fd_sta | STA_BSY;              /* set busy */
                }
            else fd_done (u, 0, 0, 0);                  /* nrml done */
            break;
            }
        else if (fd_sta & STA_IDL) {                    /* must be idle */
            if (fnc != FNC_RSTA) {                      /* !rd status */
                fd_sta = STA_BSY;                       /* busy, !idle */
                fd_es[u][0] = 0;
                fd_es[u][1] = u;                        /* init ext sta */
                }
            else fd_sta = (fd_sta & ~STA_IDL) | STA_BSY;
            if (fnc == FNC_BOOT)                        /* boot? fixed sec */
                t = LRN_BOOT;
            else if (fd_wdv)                            /* valid data? use */
                t = fd_lrn;
            else t = uptr->LRN;                         /* use prev */
            fd_wdv = 0;                                 /* data invalid */
            fd_bptr = 0;                                /* init buffer */
            uptr->FNC = fnc;                            /* save function */
            uptr->LRN = t;                              /* save LRN */
            if (ctab[fnc] & C_RD)                       /* seek now? */
                sched_seek (uptr, t);
            else sim_activate (uptr, fd_ctime);         /* start cmd */
            }
        break;
        }

return 0;
}

/* Unit service; the action to be taken depends on command */

t_stat fd_svc (UNIT *uptr)
{
uint32 i, u, tk, sc, crc, fnc, da;
uint8 *fbuf = (uint8 *) uptr->filebuf;

u = uptr - fd_dev.units;                                /* get unit number */
fnc = GET_FNC (uptr->FNC);                              /* get function */
switch (fnc) {                                          /* case on function */

    case FNC_RESET:                                     /* reset */
        fd_clr (&fd_dev);                               /* clear device */
        fd_done (u, 0, 0, 0);                           /* set idle */
        return SCPE_OK;

    case FNC_STOP:                                      /* stop */
        fd_done (u, 0, 0, 0);                           /* set idle */
        return SCPE_OK;

    case FNC_BOOT:                                      /* boot, buf empty */
    case FNC_RD:                                        /* read, buf empty */
        if (uptr->FNC & FNC_STOPPING)                   /* stopped? */
            break;
        if (fd_dte (uptr, FALSE))                       /* xfr error? */
            return SCPE_OK;
        da = GET_DA (uptr->LRN);                        /* get disk addr */
        for (i = 0; i < FD_NUMBY; i++)                  /* read sector */
            fdxb[i] = fbuf[da + i];
        if (fbuf[FD_SIZE  + uptr->LRN - 1]) {           /* deleted? set err */
            fd_sta = fd_sta | STA_DLR;
            fd_es[u][0] = fd_es[u][0] | ES0_DEL;
            }
        fd_es[u][2] = GET_SEC (uptr->LRN);              /* set ext sec/trk */
        fd_es[u][3] = GET_TRK (uptr->LRN);
        fd_bptr = 0;                                    /* init buf */
        uptr->LRN = uptr->LRN + 1;                      /* next block */
        break;

    case FNC_WR: case FNC_DEL:                          /* write block */
        if (fd_dte (uptr, TRUE))                        /* xfr error? */
            return SCPE_OK;
        if (fd_bptr) {                                  /* any transfer? */
            da = GET_DA (uptr->LRN);                    /* get disk addr */
            for (i = fd_bptr; i < FD_NUMBY; i++)        /* pad sector */
                fdxb[i] = fd_db;
            for (i = 0; i < FD_NUMBY; i++)              /* write sector */
                fbuf[da + i] = fdxb[i];                 /* then dir */
            fbuf[FD_SIZE + uptr->LRN - 1] = ((fnc == FNC_DEL)? 1: 0);
            uptr->hwmark = uptr->capac;                 /* rewrite all */
            fd_es[u][2] = GET_SEC (uptr->LRN);          /* set ext sec/trk */
            fd_es[u][3] = GET_TRK (uptr->LRN);
            fd_bptr = 0;                                /* init buf */
            uptr->LRN = uptr->LRN + 1;                  /* next block */
            }
        break;

    case FNC_RSTA:                                      /* read status */
        if (uptr->flags & UNIT_WPRT)                    /* wr protected? */
            fd_es[u][0] = fd_es[u][0] | ES0_WRP;
        if (GET_TRK (uptr->LRN) == 0)                   /* on track 0? */
            fd_es[u][1] = fd_es[u][1] | ES1_TK0;
        if ((uptr->flags & UNIT_BUF) == 0) {            /* not attached? */
            fd_es[u][0] = fd_es[u][0] | ES0_FLT;        /* set err */
            fd_es[u][1] = fd_es[u][1] | ES1_NRDY;
			}
        for (i = 0; i < ES_SIZE; i++)                   /* copy to buf */
            fdxb[i] = fd_es[u][i];
        for (i = ES_SIZE; i < FD_NUMBY; i++)
            fdxb[i] = 0;
        break;

    case FNC_RDID:                                      /* read ID */
        if ((uptr->flags & UNIT_BUF) == 0) {            /* not attached? */
            fd_done (u, STA_ERR, ES0_ERR | ES0_FLT, ES1_NRDY);
            return SCPE_OK;
			}
        for (i = 0; i < FD_NUMBY; i++) fdxb[i] = 0;     /* clr buf */
        tk = GET_TRK (uptr->LRN);                       /* get track */
        sc = GET_SEC (uptr->LRN);                       /* get sector */
        fdxb[0] = tk & 0xFF;                            /* store track */
        fdxb[2] = sc & 0xFF;                            /* store sector */
        crc = fd_crc (0xFFFF, 0xFE00, 8);               /* CRC addr mark */
        crc = fd_crc (crc, tk << 8, 16);                /* CRC track */
        crc = fd_crc (crc, sc << 8, 16);                /* CRC sector */
        fdxb[4] = (crc >> 8) & 0xFF;                    /* store CRC */
        fdxb[5] = crc & 0xFF;
        break;

    case FNC_FMT:                                       /* format */
    default:
        fd_done (u, STA_ERR, ES0_ERR, ES1_CMD);         /* ill cmd */
        uptr->LRN = 1;                                  /* on track 0 */
        return SCPE_OK;
        }

if (uptr->FNC & FNC_STOPPING) {                         /* stopping? */
    uptr->FNC = FNC_STOP;                               /* fnc = STOP */
    sim_activate (uptr, fd_ctime);                      /* schedule */
    }   
fd_sta = fd_sta & ~STA_BSY;                             /* clear busy */
if (fd_arm)                                             /* if armed, int */
    SET_INT (v_FD);
return SCPE_OK;
}

/* Schedule seek */

void sched_seek (UNIT *uptr, int32 newlrn)
{
int32 diff = newlrn - uptr->LRN;                        /* LRN diff */

if (diff < 0)                                           /* ABS */
    diff = -diff;
if (diff < 10)
    diff = 10;                                          /* MIN 10 */
sim_activate (uptr, diff * fd_stime);                   /* schedule */
return;
}

/* Command complete */

void fd_done (uint32 u, uint32 nsta, uint32 nes0, uint32 nes1)
{
fd_sta = (fd_sta | STA_IDL | nsta) & ~STA_BSY;          /* set idle */
if (fd_arm)                                             /* if armed, int */
    SET_INT (v_FD);
fd_es[u][0] = fd_es[u][0] | nes0;                       /* set ext state */
fd_es[u][1] = fd_es[u][1] | nes1;
return;
}

/* Test for data transfer error */

t_bool fd_dte (UNIT *uptr, t_bool wr)
{
uint32 u = uptr - fd_dev.units;

if ((uptr->flags & UNIT_BUF) == 0) {                    /* not attached? */
    fd_done (u, STA_ERR, ES0_ERR | ES0_FLT, ES1_NRDY);
    return TRUE;
    }
if (wr && (uptr->flags & UNIT_WPRT)) {                  /* wr protected? */
    fd_done (u, STA_ERR, ES0_ERR | ES0_WRP, 0);
    return TRUE;
    }
if ((uptr->LRN == 0) || (uptr->LRN > FD_NUMLRN)) {      /* bad LRN? */
    fd_done (u, STA_ERR, ES0_ERR | ES0_LRN, 0);
    return TRUE;
    }
return FALSE;
}

/* Header CRC calculation */

uint32 fd_crc (uint32 crc, uint32 dat, uint32 cnt)
{
uint32 i, wrk;

for (i = 0; i < cnt; i++) {
    wrk = crc ^ dat;
    crc = (crc << 1) & DMASK16;
    if (wrk & SIGN16)
        crc = ((crc ^ 0x1020) + 1) & DMASK16;
    dat = (dat << 1) & DMASK16;
    }
return crc;
}

/* Reset routine */

t_stat fd_clr (DEVICE *dptr)
{
int32 i, j;
UNIT *uptr;

fd_sta = STA_IDL;                                       /* idle */
fd_cmd = 0;                                             /* clear state */
fd_db = 0;
fd_bptr = 0;
fd_lrn = 1;
fd_wdv = 0;
for (i = 0; i < FD_NUMBY; i++) fdxb[i] = 0;             /* clr xfr buf */
for (i = 0; i < FD_NUMDR; i++) {                        /* loop thru units */
    for (j = 0; j < ES_SIZE; j++) fd_es[i][j] = 0;      /* clr ext sta */
    fd_es[i][2] = 1;                                    /* sector 1 */
    uptr = fd_dev.units + i;
    sim_cancel (uptr);                                  /* stop drive */
    uptr->LRN = 1;                                      /* clear state */
    uptr->FNC = 0;
    }
return SCPE_OK;
}

t_stat fd_reset (DEVICE *dptr)
{
CLR_INT (v_FD);                                         /* clear int */
CLR_ENB (v_FD);                                         /* disable int */
fd_arm = 0;                                             /* disarm int */
return fd_clr (dptr);;
}

/* Bootstrap routine */

#define BOOT_START      0x50
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (uint8))

static uint8 boot_rom[] = {
    0xD5, 0x00,  0x00, 0xCF,                            /* ST:  AL CF */
    0x43, 0x00,  0x00, 0x80                             /*      BR 80 */
    };

t_stat fd_boot (int32 unitno, DEVICE *dptr)
{
extern uint32 PC, dec_flgs;
extern uint16 decrom[];

if (decrom[0xD5] & dec_flgs)                            /* AL defined? */
    return SCPE_NOFNC;
IOWriteBlk (BOOT_START, BOOT_LEN, boot_rom);            /* copy boot */
IOWriteB (AL_DEV, fd_dib.dno);                          /* set dev no */
IOWriteB (AL_IOC, 0x86 + (unitno << CMD_V_UNIT));       /* set dev cmd, unit num */
IOWriteB (AL_SCH, 0);                                   /* clr sch dev no */
PC = BOOT_START;
return SCPE_OK;
}
