/* pdp8_rx.c: RX8E/RX01, RX28/RX02 floppy disk simulator

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

   rx           RX8E/RX01, RX28/RX02 floppy disk

   17-Sep-13    RMS     Changed to use central set_bootpc routine
   03-Sep-13    RMS     Added explicit void * cast
   15-May-06    RMS     Fixed bug in autosize attach (Dave Gesswein)
   04-Jan-04    RMS     Changed sim_fsize calling sequence
   05-Nov-03    RMS     Fixed bug in RX28 read status (Charles Dickman)
   26-Oct-03    RMS     Cleaned up buffer copy code, fixed double density write
   25-Apr-03    RMS     Revised for extended file support
   14-Mar-03    RMS     Fixed variable size interaction with save/restore
   03-Mar-03    RMS     Fixed autosizing
   08-Oct-02    RMS     Added DIB, device number support
                        Fixed reset to work with disabled device
   15-Sep-02    RMS     Added RX28/RX02 support
   06-Jan-02    RMS     Changed enable/disable support
   30-Nov-01    RMS     Added read only unit, extended SET/SHOW support
   24-Nov-01    RMS     Converted FLG to array
   17-Jul-01    RMS     Fixed warning from VC++ 6
   26-Apr-01    RMS     Added device enable/disable support
   13-Apr-01    RMS     Revised for register arrays
   14-Apr-99    RMS     Changed t_addr to unsigned
   15-Aug-96    RMS     Fixed bug in LCD

   An RX01 diskette consists of 77 tracks, each with 26 sectors of 128B.
   An RX02 diskette consists of 77 tracks, each with 26 sectors of 128B
   (single density) or 256B (double density).  Tracks are numbered 0-76,
   sectors 1-26.  The RX8E (RX28) can store data in 8b mode or 12b mode.
   In 8b mode, the controller reads or writes 128 bytes (128B or 256B)
   per sector.  In 12b mode, it reads or writes 64 (64 or 128) 12b words
   per sector.  The 12b words are bit packed into the first 96 (192) bytes
   of the sector; the last 32 (64) bytes are zeroed on writes.
*/

#include "pdp8_defs.h"

#define RX_NUMTR        77                              /* tracks/disk */
#define RX_M_TRACK      0377
#define RX_NUMSC        26                              /* sectors/track */
#define RX_M_SECTOR     0177                            /* cf Jones!! */
#define RX_NUMBY        128                             /* bytes/sector */
#define RX2_NUMBY       256
#define RX_NUMWD        (RX_NUMBY / 2)                  /* words/sector */
#define RX2_NUMWD       (RX2_NUMBY / 2)
#define RX_SIZE         (RX_NUMTR * RX_NUMSC * RX_NUMBY)        /* bytes/disk */
#define RX2_SIZE        (RX_NUMTR * RX_NUMSC * RX2_NUMBY)
#define RX_NUMDR        2                               /* drives/controller */
#define RX_M_NUMDR      01
#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DEN      (UNIT_V_UF + 1)                 /* double density */
#define UNIT_V_AUTO     (UNIT_V_UF + 2)                 /* autosize */
#define UNIT_WLK        (1u << UNIT_V_WLK)
#define UNIT_DEN        (1u << UNIT_V_DEN)
#define UNIT_AUTO       (1u << UNIT_V_AUTO)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

#define IDLE            0                               /* idle state */
#define CMD8            1                               /* 8b cmd, ho next */
#define RWDS            2                               /* rw, sect next */
#define RWDT            3                               /* rw, track next */
#define RWXFR           4                               /* rw, transfer */
#define FILL            5                               /* fill buffer */
#define EMPTY           6                               /* empty buffer */
#define SDCNF           7                               /* set dens, conf next */
#define SDXFR           8                               /* set dens, transfer */
#define CMD_COMPLETE    9                               /* set done next */
#define INIT_COMPLETE   10                              /* init compl next */

#define RXCS_V_FUNC     1                               /* function */
#define RXCS_M_FUNC     7
#define  RXCS_FILL      0                               /* fill buffer */
#define  RXCS_EMPTY     1                               /* empty buffer */
#define  RXCS_WRITE     2                               /* write sector */
#define  RXCS_READ      3                               /* read sector */
#define  RXCS_SDEN      4                               /* set density (RX28) */
#define  RXCS_RXES      5                               /* read status */
#define  RXCS_WRDEL     6                               /* write del data */
#define  RXCS_ECODE     7                               /* read error code */
#define RXCS_DRV        0020                            /* drive */
#define RXCS_MODE       0100                            /* mode */
#define RXCS_MAINT      0200                            /* maintenance */
#define RXCS_DEN        0400                            /* density (RX28) */
#define RXCS_GETFNC(x)  (((x) >> RXCS_V_FUNC) & RXCS_M_FUNC)

#define RXES_CRC        0001                            /* CRC error NI */
#define RXES_ID         0004                            /* init done */
#define RXES_RX02       0010                            /* RX02 (RX28) */
#define RXES_DERR       0020                            /* density err (RX28) */
#define RXES_DEN        0040                            /* density (RX28) */
#define RXES_DD         0100                            /* deleted data */
#define RXES_DRDY       0200                            /* drive ready */

#define TRACK u3                                        /* current track */
#define READ_RXDBR      ((rx_csr & RXCS_MODE)? AC | (rx_dbr & 0377): rx_dbr)
#define CALC_DA(t,s,b)  (((t) * RX_NUMSC) + ((s) - 1)) * b

extern int32 int_req, int_enable, dev_done;

int32 rx_28 = 0;                                        /* controller type */
int32 rx_tr = 0;                                        /* xfer ready flag */
int32 rx_err = 0;                                       /* error flag */
int32 rx_csr = 0;                                       /* control/status */
int32 rx_dbr = 0;                                       /* data buffer */
int32 rx_esr = 0;                                       /* error status */
int32 rx_ecode = 0;                                     /* error code */
int32 rx_track = 0;                                     /* desired track */
int32 rx_sector = 0;                                    /* desired sector */
int32 rx_state = IDLE;                                  /* controller state */
int32 rx_cwait = 100;                                   /* command time */
int32 rx_swait = 10;                                    /* seek, per track */
int32 rx_xwait = 1;                                     /* tr set time */
int32 rx_stopioe = 0;                                   /* stop on error */
uint8 rx_buf[RX2_NUMBY] = { 0 };                        /* sector buffer */
int32 rx_bptr = 0;                                      /* buffer pointer */

int32 rx (int32 IR, int32 AC);
t_stat rx_svc (UNIT *uptr);
t_stat rx_reset (DEVICE *dptr);
t_stat rx_boot (int32 unitno, DEVICE *dptr);
t_stat rx_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rx_attach (UNIT *uptr, CONST char *cptr);
void rx_cmd (void);
void rx_done (int32 esr_flags, int32 new_ecode);
t_stat rx_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rx_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* RX8E data structures

   rx_dev       RX device descriptor
   rx_unit      RX unit list
   rx_reg       RX register list
   rx_mod       RX modifier list
*/

DIB rx_dib = { DEV_RX, 1, { &rx } };

UNIT rx_unit[] = {
    { UDATA (&rx_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF+
             UNIT_ROABLE, RX_SIZE) },
    { UDATA (&rx_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF+
             UNIT_ROABLE, RX_SIZE) }
    };

REG rx_reg[] = {
    { ORDATAD (RXCS, rx_csr, 12, "status") },
    { ORDATAD (RXDB, rx_dbr, 12, "data buffer") },
    { ORDATAD (RXES, rx_esr, 12, "error status") },
    { ORDATA (RXERR, rx_ecode, 8) },
    { ORDATAD (RXTA, rx_track, 8, "current track") },
    { ORDATAD (RXSA, rx_sector, 8, "current sector") },
    { DRDATAD (STAPTR, rx_state, 4, "controller state"), REG_RO },
    { DRDATAD (BUFPTR, rx_bptr, 8, "buffer pointer")  },
    { FLDATAD (TR, rx_tr, 0, "transfer ready flag") },
    { FLDATAD (ERR, rx_err, 0, "error flag") },
    { FLDATAD (DONE, dev_done, INT_V_RX, "done flag") },
    { FLDATAD (ENABLE, int_enable, INT_V_RX, "interrupt enable flag") },
    { FLDATAD (INT, int_req, INT_V_RX, "interrupt pending flag") },
    { DRDATAD (CTIME, rx_cwait, 24, "command completion time"), PV_LEFT },
    { DRDATAD (STIME, rx_swait, 24, "seek time per track"), PV_LEFT },
    { DRDATAD (XTIME, rx_xwait, 24, "transfer ready delay"), PV_LEFT },
    { FLDATAD (STOP_IOE, rx_stopioe, 0, "stop on I/O error") },
    { BRDATAD (SBUF, rx_buf, 8, 8, RX2_NUMBY, "sector buffer array") },
    { FLDATA (RX28, rx_28, 0), REG_HRO },
    { URDATA (CAPAC, rx_unit[0].capac, 10, T_ADDR_W, 0,
              RX_NUMDR, REG_HRO | PV_LEFT) },
    { ORDATA (DEVNUM, rx_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB rx_mod[] = {
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "RX28", &rx_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "RX8E", &rx_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 0, "TYPE", NULL, NULL, &rx_showtype, NULL },
    { (UNIT_DEN+UNIT_ATT), UNIT_ATT, "single density", NULL, NULL },
    { (UNIT_DEN+UNIT_ATT), (UNIT_DEN+UNIT_ATT), "double density", NULL, NULL },
    { (UNIT_AUTO+UNIT_DEN+UNIT_ATT), 0, "single density", NULL, NULL },
    { (UNIT_AUTO+UNIT_DEN+UNIT_ATT), UNIT_DEN, "double density", NULL, NULL },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
    { UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL },
    { (UNIT_AUTO+UNIT_DEN), 0, NULL, "SINGLE", &rx_set_size },
    { (UNIT_AUTO+UNIT_DEN), UNIT_DEN, NULL, "DOUBLE", &rx_set_size },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE rx_dev = {
    "RX", rx_unit, rx_reg, rx_mod,
    RX_NUMDR, 8, 20, 1, 8, 8,
    NULL, NULL, &rx_reset,
    &rx_boot, &rx_attach, NULL,
    &rx_dib, DEV_DISABLE
    };

/* IOT routine */

int32 rx (int32 IR, int32 AC)
{
int32 drv = ((rx_csr & RXCS_DRV)? 1: 0);                /* get drive number */

switch (IR & 07) {                                      /* decode IR<9:11> */

    case 0:                                             /* unused */
        break;

    case 1:                                             /* LCD */
        if (rx_state != IDLE)                           /* ignore if busy */
            return AC;
        dev_done = dev_done & ~INT_RX;                  /* clear done, int */
        int_req = int_req & ~INT_RX;
        rx_tr = rx_err = 0;                             /* clear flags */
        rx_bptr = 0;                                    /* clear buf pointer */
        if (rx_28 && (AC & RXCS_MODE)) {                /* RX28 8b mode? */
            rx_dbr = rx_csr = AC & 0377;                /* save 8b */
            rx_tr = 1;                                  /* xfer is ready */
            rx_state = CMD8;                            /* wait for part 2 */
            }
        else {
            rx_dbr = rx_csr = AC;                       /* save new command */
            rx_cmd ();                                  /* issue command */
            }
        return 0;                                       /* clear AC */

    case 2:                                             /* XDR */
        switch (rx_state & 017) {                       /* case on state */

        case EMPTY:                                     /* emptying buffer */
            sim_activate (&rx_unit[drv], rx_xwait);     /* sched xfer */
            return READ_RXDBR;                          /* return data reg */

        case CMD8:                                      /* waiting for cmd */
            rx_dbr = AC & 0377;
            rx_csr = (rx_csr & 0377) | ((AC & 017) << 8);
            rx_cmd ();
            break;

        case RWDS:case RWDT:case FILL:case SDCNF:       /* waiting for data */
            rx_dbr = AC;                                /* save data */
            sim_activate (&rx_unit[drv], rx_xwait);     /* schedule */
            break;

        default:                                        /* default */
            return READ_RXDBR;                          /* return data reg */
            }
        break;

    case 3:                                             /* STR */
        if (rx_tr != 0) {
            rx_tr = 0;
            return IOT_SKP + AC;
            }
        break;

    case 4:                                             /* SER */
        if (rx_err != 0) {
            rx_err = 0;
            return IOT_SKP + AC;
            }
        break;

    case 5:                                             /* SDN */
        if ((dev_done & INT_RX) != 0) {
            dev_done = dev_done & ~INT_RX;
            int_req = int_req & ~INT_RX;
            return IOT_SKP + AC;
            }
        break;

    case 6:                                             /* INTR */
        if (AC & 1)
            int_enable = int_enable | INT_RX;
        else int_enable = int_enable & ~INT_RX;
        int_req = INT_UPDATE;
        break;

    case 7:                                             /* INIT */
        rx_reset (&rx_dev);                             /* reset device */
        break;
        }                                               /* end case pulse */

return AC;
}

void rx_cmd (void)
{
int32 drv = ((rx_csr & RXCS_DRV)? 1: 0);                /* get drive number */

switch (RXCS_GETFNC (rx_csr)) {                         /* decode command */

    case RXCS_FILL:
        rx_state = FILL;                                /* state = fill */
        rx_tr = 1;                                      /* xfer is ready */
        rx_esr = rx_esr & RXES_ID;                      /* clear errors */
        break;

    case RXCS_EMPTY:
        rx_state = EMPTY;                               /* state = empty */
        rx_esr = rx_esr & RXES_ID;                      /* clear errors */
        sim_activate (&rx_unit[drv], rx_xwait);         /* sched xfer */
        break;

    case RXCS_READ: case RXCS_WRITE: case RXCS_WRDEL:
        rx_state = RWDS;                                /* state = get sector */
        rx_tr = 1;                                      /* xfer is ready */
        rx_esr = rx_esr & RXES_ID;                      /* clear errors */
        break;

    case RXCS_SDEN:
        if (rx_28) {                                    /* RX28? */
            rx_state = SDCNF;                           /* state = get conf */
            rx_tr = 1;                                  /* xfer is ready */
            rx_esr = rx_esr & RXES_ID;                  /* clear errors */
            break;
            }                                           /* else fall thru */
    default:
        rx_state = CMD_COMPLETE;                        /* state = cmd compl */
        sim_activate (&rx_unit[drv], rx_cwait);         /* sched done */
        break;
        }                                               /* end switch func */

return;
}

/* Unit service; the action to be taken depends on the transfer state:

   IDLE         Should never get here
   RWDS         Save sector, set TR, set RWDT
   RWDT         Save track, set RWXFR
   RWXFR        Read/write buffer
   FILL         copy dbr to rx_buf[rx_bptr], advance ptr
                if rx_bptr > max, finish command, else set tr
   EMPTY        if rx_bptr > max, finish command, else
                copy rx_buf[rx_bptr] to dbr, advance ptr, set tr
   CMD_COMPLETE copy requested data to dbr, finish command
   INIT_COMPLETE read drive 0, track 1, sector 1 to buffer, finish command

   For RWDT and CMD_COMPLETE, the input argument is the selected drive;
   otherwise, it is drive 0.
*/

t_stat rx_svc (UNIT *uptr)
{
int32 i, func, byptr, bps, wps;
int8 *fbuf = (int8 *) uptr->filebuf;
uint32 da;
#define PTR12(x) (((x) + (x) + (x)) >> 1)

if (rx_28 && (uptr->flags & UNIT_DEN))                  /* RX28 and double density? */
    bps = RX2_NUMBY;                                    /* double bytes/sector */
else bps = RX_NUMBY;                                    /* RX8E, normal count */
wps = bps / 2;
func = RXCS_GETFNC (rx_csr);                            /* get function */
switch (rx_state) {                                     /* case on state */

    case IDLE:                                          /* idle */
        return SCPE_IERR;

    case EMPTY:                                         /* empty buffer */
        if (rx_csr & RXCS_MODE) {                       /* 8b xfer? */
            if (rx_bptr >= bps) {                       /* done? */
                rx_done (0, 0);                         /* set done */
                break;                                  /* and exit */
                }
            rx_dbr = rx_buf[rx_bptr];                   /* else get data */
            }
        else {
            byptr = PTR12 (rx_bptr);                    /* 12b xfer */
            if (rx_bptr >= wps) {                       /* done? */
                rx_done (0, 0);                         /* set done */
                break;                                  /* and exit */
                }
            rx_dbr = (rx_bptr & 1)?                     /* get data */
                ((rx_buf[byptr] & 017) << 8) | rx_buf[byptr + 1]:
                (rx_buf[byptr] << 4) | ((rx_buf[byptr + 1] >> 4) & 017);
            }
        rx_bptr = rx_bptr + 1;
        rx_tr = 1;
        break;

    case FILL:                                          /* fill buffer */
        if (rx_csr & RXCS_MODE) {                       /* 8b xfer? */
            rx_buf[rx_bptr] = rx_dbr;                   /* fill buffer */
            rx_bptr = rx_bptr + 1;
            if (rx_bptr < bps)                          /* if more, set xfer */
                rx_tr = 1;
            else rx_done (0, 0);                        /* else done */
            }
        else {
            byptr = PTR12 (rx_bptr);                    /* 12b xfer */
            if (rx_bptr & 1) {                          /* odd or even? */
                rx_buf[byptr] = (rx_buf[byptr] & 0360) | ((rx_dbr >> 8) & 017);
                rx_buf[byptr + 1] = rx_dbr & 0377;
                }
            else {
                rx_buf[byptr] = (rx_dbr >> 4) & 0377;
                rx_buf[byptr + 1] = (rx_dbr & 017) << 4;
                }
            rx_bptr = rx_bptr + 1;
            if (rx_bptr < wps)                          /* if more, set xfer */
                rx_tr = 1;
            else {
                for (i = PTR12 (wps); i < bps; i++)
                    rx_buf[i] = 0;                      /* else fill sector */
                rx_done (0, 0);                         /* set done */
                }
            }
        break;

    case RWDS:                                          /* wait for sector */
        rx_sector = rx_dbr & RX_M_SECTOR;               /* save sector */
        rx_tr = 1;                                      /* set xfer ready */
        rx_state = RWDT;                                /* advance state */
        return SCPE_OK;

    case RWDT:                                          /* wait for track */
        rx_track = rx_dbr & RX_M_TRACK;                 /* save track */
        rx_state = RWXFR;
        sim_activate (uptr,                             /* sched done */
            rx_swait * abs (rx_track - uptr->TRACK));
        return SCPE_OK;

    case RWXFR:                                         /* transfer */
        if ((uptr->flags & UNIT_BUF) == 0) {            /* not buffered? */
            rx_done (0, 0110);                          /* done, error */
            return IORETURN (rx_stopioe, SCPE_UNATT);
            }
        if (rx_track >= RX_NUMTR) {                     /* bad track? */
            rx_done (0, 0040);                          /* done, error */
            break;
            }
        uptr->TRACK = rx_track;                         /* now on track */
        if ((rx_sector == 0) || (rx_sector > RX_NUMSC)) {       /* bad sect? */
            rx_done (0, 0070);                          /* done, error */
            break;
            }
        if (rx_28 &&                                    /* RX28? */
            (((uptr->flags & UNIT_DEN) != 0) ^
             ((rx_csr & RXCS_DEN) != 0))) {             /* densities agree? */
            rx_done (RXES_DERR, 0240);                  /* no, error */
            break;
            }
        da = CALC_DA (rx_track, rx_sector, bps);        /* get disk address */
        if (func == RXCS_WRDEL)                         /* del data? */
            rx_esr = rx_esr | RXES_DD;
        if (func == RXCS_READ) {                        /* read? */
            for (i = 0; i < bps; i++) rx_buf[i] = fbuf[da + i];
            }
        else {                                          /* write */
            if (uptr->flags & UNIT_WPRT) {              /* locked? */
                rx_done (0, 0100);                      /* done, error */
                break;
                }
            for (i = 0; i < bps; i++)
                fbuf[da + i] = rx_buf[i];
            da = da + bps;
            if (da > uptr->hwmark)
                uptr->hwmark = da;
            }
        rx_done (0, 0);                                 /* done */
        break;

    case SDCNF:                                         /* confirm set density */
        if ((rx_dbr & 0377) != 0111) {                  /* confirmed? */
            rx_done (0, 0250);                          /* no, error */
            break;
            }
        rx_state = SDXFR;                               /* next state */
        sim_activate (uptr, rx_cwait * 100);            /* schedule operation */
        break;

    case SDXFR:                                         /* erase disk */
        for (i = 0; i < (int32) uptr->capac; i++)
            fbuf[i] = 0;
        uptr->hwmark = uptr->capac;
        if (rx_csr & RXCS_DEN)
            uptr->flags = uptr->flags | UNIT_DEN;
        else uptr->flags = uptr->flags & ~UNIT_DEN;
        rx_done (0, 0);
        break;

    case CMD_COMPLETE:                                  /* command complete */
        if (func == RXCS_ECODE) {                       /* read ecode? */
            rx_dbr = rx_ecode;                          /* set dbr */
            rx_done (0, -1);                            /* don't update */
            }
        else if (rx_28) {                               /* no, read sta; RX28? */
            rx_esr = rx_esr & ~RXES_DERR;               /* assume dens match */
            if (((uptr->flags & UNIT_DEN) != 0) ^       /* densities mismatch? */
                ((rx_csr & RXCS_DEN) != 0))
                rx_done (RXES_DERR, 0240);              /* yes, error */
            else rx_done (0, 0);                        /* no, ok */
            }
        else rx_done (0, 0);                            /* RX8E status */
        break;

    case INIT_COMPLETE:                                 /* init complete */
        rx_unit[0].TRACK = 1;                           /* drive 0 to trk 1 */
        rx_unit[1].TRACK = 0;                           /* drive 1 to trk 0 */
        if ((rx_unit[0].flags & UNIT_BUF) == 0) {       /* not buffered? */
            rx_done (RXES_ID, 0010);                    /* init done, error */
            break;
            }
        da = CALC_DA (1, 1, bps);                       /* track 1, sector 1 */
        for (i = 0; i < bps; i++)                       /* read sector */
            rx_buf[i] = fbuf[da + i];
        rx_done (RXES_ID, 0);                           /* set done */
        if ((rx_unit[1].flags & UNIT_ATT) == 0)
            rx_ecode = 0020;
        break;
        }                                               /* end case state */

return SCPE_OK;
}

/* Command complete.  Set done and put final value in interface register,
   return to IDLE state.
*/

void rx_done (int32 esr_flags, int32 new_ecode)
{
int32 drv = (rx_csr & RXCS_DRV)? 1: 0;

rx_state = IDLE;                                        /* now idle */
dev_done = dev_done | INT_RX;                           /* set done */
int_req = INT_UPDATE;                                   /* update ints */
rx_esr = (rx_esr | esr_flags) & ~(RXES_DRDY|RXES_RX02|RXES_DEN);
if (rx_28)                                              /* RX28? */
    rx_esr = rx_esr | RXES_RX02;
if (rx_unit[drv].flags & UNIT_ATT) {                    /* update drv rdy */
    rx_esr = rx_esr | RXES_DRDY;
    if (rx_unit[drv].flags & UNIT_DEN)                  /* update density */
        rx_esr = rx_esr | RXES_DEN;
    }
if (new_ecode > 0)                                      /* test for error */
    rx_err = 1;
if (new_ecode < 0)                                      /* don't update? */
    return;
rx_ecode = new_ecode;                                   /* update ecode */
rx_dbr = rx_esr;                                        /* update RXDB */
return;
}

/* Reset routine.  The RX is one of the few devices that schedules
   an I/O transfer as part of its initialization */

t_stat rx_reset (DEVICE *dptr)
{
rx_dbr = rx_csr = 0;                                    /* 12b mode, drive 0 */
rx_esr = rx_ecode = 0;                                  /* clear error */
rx_tr = rx_err = 0;                                     /* clear flags */
rx_track = rx_sector = 0;                               /* clear address */
rx_state = IDLE;                                        /* ctrl idle */
dev_done = dev_done & ~INT_RX;                          /* clear done, int */
int_req = int_req & ~INT_RX;
int_enable = int_enable & ~INT_RX;
sim_cancel (&rx_unit[1]);                               /* cancel drive 1 */
if (dptr->flags & DEV_DIS)                              /* disabled? */
    sim_cancel (&rx_unit[0]);
else if (rx_unit[0].flags & UNIT_BUF)  {                /* attached? */
    rx_state = INIT_COMPLETE;                           /* yes, sched init */
    sim_activate (&rx_unit[0], rx_swait * abs (1 - rx_unit[0].TRACK));
    }
else rx_done (rx_esr | RXES_ID, 0010);                  /* no, error */
return SCPE_OK;
}

/* Attach routine */

t_stat rx_attach (UNIT *uptr, CONST char *cptr)
{
uint32 sz;

if ((uptr->flags & UNIT_AUTO) && (sz = sim_fsize_name (cptr))) {
    if (sz > RX_SIZE)
        uptr->flags = uptr->flags | UNIT_DEN;
    else uptr->flags = uptr->flags & ~UNIT_DEN;
    }
uptr->capac = (uptr->flags & UNIT_DEN)? RX2_SIZE: RX_SIZE;
return attach_unit (uptr, cptr);
}

/* Set size routine */

t_stat rx_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
if ((rx_28 == 0) && val)                                /* not on RX8E */
    return SCPE_NOFNC;
uptr->capac = val? RX2_SIZE: RX_SIZE;
return SCPE_OK;
}

/* Set controller type */

t_stat rx_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i;

if ((val < 0) || (val > 1) || (cptr != NULL))
    return SCPE_ARG;
if (val == rx_28)
    return SCPE_OK;
for (i = 0; i < RX_NUMDR; i++) {
    if (rx_unit[i].flags & UNIT_ATT)
        return SCPE_ALATT;
    }
for (i = 0; i < RX_NUMDR; i++) {
    if (val)
        rx_unit[i].flags = rx_unit[i].flags | UNIT_DEN | UNIT_AUTO;
    else rx_unit[i].flags = rx_unit[i].flags & ~(UNIT_DEN | UNIT_AUTO);
    rx_unit[i].capac = val? RX2_SIZE: RX_SIZE;
    }
rx_28 = val;
return SCPE_OK;
}

/* Show controller type */

t_stat rx_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (rx_28) fprintf (st, "RX28");
else fprintf (st, "RX8E");
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START      022
#define BOOT_ENTRY      022
#define BOOT_INST       060
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))
#define BOOT2_START     020
#define BOOT2_ENTRY     033
#define BOOT2_LEN       (sizeof (boot2_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    06755,                      /* 22, SDN */
    05022,                      /* 23, JMP .-1 */
    07126,                      /* 24, CLL CML RTL      ; read command + */
    01060,                      /* 25, TAD UNIT         ; unit no */
    06751,                      /* 26, LCD              ; load read+unit */
    07201,                      /* 27, CLA IAC          ; AC = 1 */
    04053,                      /* 30, JMS LOAD         ; load sector */
    04053,                      /* 31, JMS LOAD         ; load track */
    07104,                      /* 32, CLL RAL          ; AC = 2 */
    06755,                      /* 33, SDN */
    05054,                      /* 34, JMP LOAD+1 */
    06754,                      /* 35, SER */
    07450,                      /* 36, SNA              ; more to do? */
    07610,                      /* 37, CLA SKP          ; error */
    05046,                      /* 40, JMP 46           ; go empty */
    07402,                      /* 41-45, HALT          ; error */
    07402,
    07402,
    07402,
    07402,
    06751,                      /* 46, LCD              ; load empty */
    04053,                      /* 47, JMS LOAD         ; get data */
    03002,                      /* 50, DCA 2            ; store */
    02050,                      /* 51, ISZ 50           ; incr store */
    05047,                      /* 52, JMP 47           ; loop until done */
    00000,                      /* LOAD, 0 */
    06753,                      /* 54, STR */
    05033,                      /* 55, JMP 33 */
    06752,                      /* 56, XDR */
    05453,                      /* 57, JMP I LOAD */
    07024,                      /* UNIT, CML RAL        ; for unit 1 */
    06030                       /* 61, KCC */
    };

static const uint16 boot2_rom[] = {
    01061,                      /* READ, TAD UNIT       ; next unit+den */
    01046,                      /* 21, TAD CON360       ; add in 360 */
    00060,                      /* 22, AND CON420       ; mask to 420 */
    03061,                      /* 23, DCA UNIT         ; 400,420,0,20... */
    07327,                      /* 24, STL CLA IAC RTL  ; AC = 6 = read */
    01061,                      /* 25, TAD UNIT         ; +unit+den */
    06751,                      /* 26, LCD              ; load cmd */
    07201,                      /* 27, CLA IAC;         ; AC = 1 = trksec */
    04053,                      /* 30, JMS LOAD         ; load trk */
    04053,                      /* 31, JMS LOAD         ; load sec */
    07004,                      /* CN7004, RAL          ; AC = 2 = empty */
    06755,                      /* START, SDN           ; done? */
    05054,                      /* 34, JMP LOAD+1       ; check xfr */
    06754,                      /* 35, SER              ; error? */
    07450,                      /* 36, SNA              ; AC=0 on start */
    05020,                      /* 37, JMP RD           ; try next den,un */
    01061,                      /* 40, TAD UNIT         ; +unit+den */
    06751,                      /* 41, LCD              ; load cmd */
    01061,                      /* 42, TAD UNIT         ; set 60 for sec boot */
    00046,                      /* 43, AND CON360       ; only density */
    01032,                      /* 44, TAD CN7004       ; magic */
    03060,                      /* 45, DCA 60 */
    00360,                      /* CON360, 360          ; NOP */
    04053,                      /* 47, JMS LOAD         ; get data */
    03002,                      /* 50, DCA 2            ; store */
    02050,                      /* 51, ISZ .-1          ; incr store */
    05047,                      /* 52, JMP .-3          ; loop until done */
    00000,                      /* LOAD, 0 */
    06753,                      /* 54, STR              ; xfr ready? */
    05033,                      /* 55, JMP 33           ; no, chk done */
    06752,                      /* 56, XDR              ; get word */
    05453,                      /* 57, JMP I 53         ; return */
    00420,                      /* CON420, 420          ; toggle */
    00020                       /* UNIT, 20             ; unit+density */
    };

t_stat rx_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern uint16 M[];

if (rx_dib.dev != DEV_RX)                               /* only std devno */
    return STOP_NOTSTD;
if (rx_28) {
    for (i = 0; i < BOOT2_LEN; i++)
        M[BOOT2_START + i] = boot2_rom[i];
    cpu_set_bootpc (BOOT2_ENTRY);
    }
else {
    for (i = 0; i < BOOT_LEN; i++)
        M[BOOT_START + i] = boot_rom[i];
    M[BOOT_INST] = unitno? 07024: 07004;
    cpu_set_bootpc (BOOT_ENTRY);
    }
return SCPE_OK;
}
