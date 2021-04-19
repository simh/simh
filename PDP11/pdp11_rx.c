/* pdp11_rx.c: RX11/RX01 floppy disk simulator

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

   rx           RX11/RX01 floppy disk

   23-Oct-13    RMS     Revised for new boot setup routine
   03-Sep-13    RMS     Added explicit void * cast
   07-Jul-05    RMS     Removed extraneous externs
   12-Oct-02    RMS     Added autoconfigure support
   08-Oct-02    RMS     Added variable address support to bootstrap
                        Added vector change/display support
                        Revised state machine based on RX211
                        New data structures
                        Fixed reset of disabled device
   26-Jan-02    RMS     Revised bootstrap to conform to M9312
   06-Jan-02    RMS     Revised enable/disable support
   30-Nov-01    RMS     Added read only unit, extended SET/SHOW support
   24-Nov-01    RMS     Converted FLG to array
   07-Sep-01    RMS     Revised device disable and interrupt mechanisms
   17-Jul-01    RMS     Fixed warning from VC++ 6.0
   26-Apr-01    RMS     Added device enable/disable support
   13-Apr-01    RMS     Revised for register arrays
   15-Feb-01    RMS     Corrected bootstrap string
   14-Apr-99    RMS     Changed t_addr to unsigned

   An RX01 diskette consists of 77 tracks, each with 26 sectors of 128B.
   Tracks are numbered 0-76, sectors 1-26.
*/

#include "pdp11_defs.h"

#define RX_NUMTR        77                              /* tracks/disk */
#define RX_M_TRACK      0377
#define RX_NUMSC        26                              /* sectors/track */
#define RX_M_SECTOR     0177
#define RX_NUMBY        128                             /* bytes/sector */
#define RX_SIZE         (RX_NUMTR * RX_NUMSC * RX_NUMBY)        /* bytes/disk */
#define RX_NUMDR        2                               /* drives/controller */
#define RX_M_NUMDR      01

#define IDLE            0                               /* idle state */
#define RWDS            1                               /* rw, sect next */
#define RWDT            2                               /* rw, track next */
#define RWXFR           3                               /* rw, transfer */
#define FILL            4                               /* fill buffer */
#define EMPTY           5                               /* empty buffer */
#define CMD_COMPLETE    6                               /* set done next */
#define INIT_COMPLETE   7                               /* init compl next */

#define RXCS_V_FUNC     1                               /* function */
#define RXCS_M_FUNC     7
#define  RXCS_FILL      0                               /* fill buffer */
#define  RXCS_EMPTY     1                               /* empty buffer */
#define  RXCS_WRITE     2                               /* write sector */
#define  RXCS_READ      3                               /* read sector */
#define  RXCS_RXES      5                               /* read status */
#define  RXCS_WRDEL     6                               /* write del data */
#define  RXCS_ECODE     7                               /* read error code */
#define RXCS_V_DRV      4                               /* drive select */
#define RXCS_V_DONE     5                               /* done */
#define RXCS_V_IE       6                               /* intr enable */
#define RXCS_V_TR       7                               /* xfer request */
#define RXCS_V_INIT     14                              /* init */
#define RXCS_V_ERR      15                              /* error */
#define RXCS_FUNC       (RXCS_M_FUNC << RXCS_V_FUNC)
#define RXCS_DRV        (1u << RXCS_V_DRV)
#define RXCS_DONE       (1u << RXCS_V_DONE)
#define RXCS_IE         (1u << RXCS_V_IE)
#define RXCS_TR         (1u << RXCS_V_TR)
#define RXCS_INIT       (1u << RXCS_V_INIT)
#define RXCS_ERR        (1u << RXCS_V_ERR)
#define RXCS_ROUT       (RXCS_ERR+RXCS_TR+RXCS_IE+RXCS_DONE)
#define RXCS_IMP        (RXCS_ROUT+RXCS_DRV+RXCS_FUNC)
#define RXCS_RW         (RXCS_IE)                       /* read/write */
#define RXCS_GETFNC(x)  (((x) >> RXCS_V_FUNC) & RXCS_M_FUNC)

#define RXES_CRC        0001                            /* CRC error */
#define RXES_PAR        0002                            /* parity error */
#define RXES_ID         0004                            /* init done */
#define RXES_WLK        0010                            /* write protect */
#define RXES_DD         0100                            /* deleted data */
#define RXES_DRDY       0200                            /* drive ready */

#define TRACK u3                                        /* current track */
#define CALC_DA(t,s) (((t) * RX_NUMSC) + ((s) - 1)) * RX_NUMBY

int32 rx_csr = 0;                                       /* control/status */
int32 rx_dbr = 0;                                       /* data buffer */
int32 rx_esr = 0;                                       /* error status */
int32 rx_ecode = 0;                                     /* error code */
int32 rx_track = 0;                                     /* desired track */
int32 rx_sector = 0;                                    /* desired sector */
int32 rx_state = IDLE;                                  /* controller state */
int32 rx_stopioe = 1;                                   /* stop on error */
int32 rx_cwait = 100;                                   /* command time */
int32 rx_swait = 10;                                    /* seek, per track */
int32 rx_xwait = 1;                                     /* tr set time */
uint8 rx_buf[RX_NUMBY] = { 0 };                         /* sector buffer */
int32 rx_bptr = 0;                                      /* buffer pointer */
int32 rx_enb = 1;                                       /* device enable */

t_stat rx_rd (int32 *data, int32 PA, int32 access);
t_stat rx_wr (int32 data, int32 PA, int32 access);
t_stat rx_svc (UNIT *uptr);
t_stat rx_reset (DEVICE *dptr);
t_stat rx_boot (int32 unitno, DEVICE *dptr);
void rx_done (int32 esr_flags, int32 new_ecode);

/* RX11 data structures

   rx_dev       RX device descriptor
   rx_unit      RX unit list
   rx_reg       RX register list
   rx_mod       RX modifier list
*/

#define IOLN_RX         004

DIB rx_dib = {
    IOBA_AUTO, IOLN_RX, &rx_rd, &rx_wr,
    1, IVCL (RX), VEC_AUTO, { NULL }, IOLN_RX,
    };

UNIT rx_unit[] = {
    { UDATA (&rx_svc,
             UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, RX_SIZE) },
    { UDATA (&rx_svc,
             UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, RX_SIZE) }
    };

REG rx_reg[] = {
    { ORDATA (RXCS, rx_csr, 16) },
    { ORDATA (RXDB, rx_dbr, 8) },
    { ORDATA (RXES, rx_esr, 8) },
    { ORDATA (RXERR, rx_ecode, 8) },
    { ORDATA (RXTA, rx_track, 8) },
    { ORDATA (RXSA, rx_sector, 8) },
    { DRDATA (STAPTR, rx_state, 3), REG_RO },
    { DRDATA (BUFPTR, rx_bptr, 7)  },
    { FLDATA (INT, IREQ (RX), INT_V_RX) },
    { FLDATA (ERR, rx_csr, RXCS_V_ERR) },
    { FLDATA (TR, rx_csr, RXCS_V_TR) },
    { FLDATA (IE, rx_csr, RXCS_V_IE) },
    { FLDATA (DONE, rx_csr, RXCS_V_DONE) },
    { DRDATA (CTIME, rx_cwait, 24), PV_LEFT },
    { DRDATA (STIME, rx_swait, 24), PV_LEFT },
    { DRDATA (XTIME, rx_xwait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, rx_stopioe, 0) },
    { BRDATA (SBUF, rx_buf, 8, 8, RX_NUMBY) },
    { ORDATA (DEVADDR, rx_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, rx_dib.vec, 16), REG_HRO },
    { NULL }
    };

MTAB rx_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable floppy drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock floppy drive" },
#if defined (VM_PDP11)
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
      &set_addr_flt, NULL, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
      &set_vec, &show_vec, NULL },
#else
    { MTAB_XTD|MTAB_VDV, 004, "ADDRESS", NULL,
      NULL, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
      NULL, &show_vec, NULL },
#endif
    { 0 }
    };

DEVICE rx_dev = {
    "RX", rx_unit, rx_reg, rx_mod,
    RX_NUMDR, 8, 20, 1, 8, 8,
    NULL, NULL, &rx_reset,
    &rx_boot, NULL, NULL,
    &rx_dib, DEV_DISABLE | DEV_UBUS | DEV_QBUS
    };

/* I/O dispatch routine, I/O addresses 17777170 - 17777172

   17777170             floppy CSR
   17777172             floppy data register
*/

t_stat rx_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 1) {                                /* decode PA<1> */

    case 0:                                             /* RXCS */
        rx_csr = rx_csr & RXCS_IMP;                     /* clear junk */
        *data = rx_csr & RXCS_ROUT;
        break;

    case 1:                                             /* RXDB */
        if ((rx_state == EMPTY) && (rx_csr & RXCS_TR)) {/* empty? */
            sim_activate (&rx_unit[0], rx_xwait);
            rx_csr = rx_csr & ~RXCS_TR;                 /* clear xfer */
            }
        *data = rx_dbr;                                 /* return data */
        break;
        }                                               /* end switch PA */

return SCPE_OK;
}

t_stat rx_wr (int32 data, int32 PA, int32 access)
{
int32 drv;

switch ((PA >> 1) & 1) {                                /* decode PA<1> */

/* Writing RXCS, three cases:
   1. Writing INIT, reset device
   2. Idle and writing new function
        - clear error, done, transfer ready, int req
        - save int enable, function, drive
        - start new function
   3. Otherwise, write IE and update interrupts
*/

    case 0:                                             /* RXCS */
        rx_csr = rx_csr & RXCS_IMP;                     /* clear junk */
        if (access == WRITEB) data = (PA & 1)?          /* write byte? */
            (rx_csr & 0377) | (data << 8): (rx_csr & ~0377) | data;
        if (data & RXCS_INIT) {                         /* initialize? */
            rx_reset (&rx_dev);                         /* reset device */
            return SCPE_OK;                             /* end if init */
            }
        if ((data & CSR_GO) && (rx_state == IDLE)) {    /* new function? */
            rx_csr = data & (RXCS_IE + RXCS_DRV + RXCS_FUNC);
            drv = ((rx_csr & RXCS_DRV)? 1: 0);          /* reselect drive */
            rx_bptr = 0;                                /* clear buf pointer */
            switch (RXCS_GETFNC (data)) {               /* case on func */

            case RXCS_FILL:
                rx_state = FILL;                        /* state = fill */
                rx_csr = rx_csr | RXCS_TR;              /* xfer is ready */
                break;

            case RXCS_EMPTY:
                rx_state = EMPTY;                       /* state = empty */
                sim_activate (&rx_unit[drv], rx_xwait);
                break;

            case RXCS_READ: case RXCS_WRITE: case RXCS_WRDEL:
                rx_state = RWDS;                        /* state = get sector */
                rx_csr = rx_csr | RXCS_TR;              /* xfer is ready */
                rx_esr = rx_esr & RXES_ID;              /* clear errors */
                break;

            default:
                rx_state = CMD_COMPLETE;                /* state = cmd compl */
                sim_activate (&rx_unit[drv], rx_cwait);
                break;
                }                                       /* end switch func */
            return SCPE_OK;
            }                                           /* end if GO */
        if ((data & RXCS_IE) == 0)
            CLR_INT (RX);
        else if ((rx_csr & (RXCS_DONE + RXCS_IE)) == RXCS_DONE)
            SET_INT (RX);
        rx_csr = (rx_csr & ~RXCS_RW) | (data & RXCS_RW);
        break;                                          /* end case RXCS */

/* Accessing RXDB, two cases:
   1. Write idle, write
   2. Write not idle and TR set, state dependent
*/

    case 1:                                             /* RXDB */
        if ((PA & 1) || ((rx_state != IDLE) && ((rx_csr & RXCS_TR) == 0)))
            return SCPE_OK;                             /* if ~IDLE, need tr */
        rx_dbr = data & 0377;                           /* save data */
        if ((rx_state != IDLE) && (rx_state != EMPTY)) {
            drv = ((rx_csr & RXCS_DRV)? 1: 0);          /* select drive */
            sim_activate (&rx_unit[drv], rx_xwait);     /* sched event */
            rx_csr = rx_csr & ~RXCS_TR;                 /* clear xfer */
            }
        break;                                          /* end case RXDB */
        }                                               /* end switch PA */

return SCPE_OK;
}

/* Unit service; the action to be taken depends on the transfer state:

   IDLE         Should never get here
   RWDS         Save sector, set TR, set RWDT
   RWDT         Save track, set RWXFR
   RWXFR        Read/write buffer
   FILL         copy ir to rx_buf[rx_bptr], advance ptr
                if rx_bptr > max, finish command, else set tr
   EMPTY        if rx_bptr > max, finish command, else
                copy rx_buf[rx_bptr] to ir, advance ptr, set tr
   CMD_COMPLETE copy requested data to ir, finish command
   INIT_COMPLETE read drive 0, track 1, sector 1 to buffer, finish command

   For RWDT and CMD_COMPLETE, the input argument is the selected drive;
   otherwise, it is drive 0.
*/

t_stat rx_svc (UNIT *uptr)
{
int32 i, func;
uint32 da;
int8 *fbuf = (int8 *) uptr->filebuf;

func = RXCS_GETFNC (rx_csr);                            /* get function */
switch (rx_state) {                                     /* case on state */

    case IDLE:                                          /* idle */
        return SCPE_IERR;                               /* done */

    case EMPTY:                                         /* empty buffer */
        if (rx_bptr >= RX_NUMBY)                        /* done all? */
            rx_done (0, 0);
        else {
            rx_dbr = rx_buf[rx_bptr];                   /* get next */
            rx_bptr = rx_bptr + 1;
            rx_csr = rx_csr | RXCS_TR;                  /* set xfer */
            }
        break;

    case FILL:                                          /* fill buffer */
        rx_buf[rx_bptr] = (uint8)rx_dbr;                /* write next */
        rx_bptr = rx_bptr + 1;
        if (rx_bptr < RX_NUMBY)                         /* more? set xfer */
            rx_csr = rx_csr | RXCS_TR;
        else rx_done (0, 0);                            /* else done */
        break;

    case RWDS:                                          /* wait for sector */
        rx_sector = rx_dbr & RX_M_SECTOR;               /* save sector */
        rx_csr = rx_csr | RXCS_TR;                      /* set xfer */
        rx_state = RWDT;                                /* advance state */
        return SCPE_OK;

    case RWDT:                                          /* wait for track */
        rx_track = rx_dbr & RX_M_TRACK;                 /* save track */
        rx_state = RWXFR;
        sim_activate (uptr,                             /* sched done */
                rx_swait * abs (rx_track - uptr->TRACK));
        return SCPE_OK;

    case RWXFR:
        if ((uptr->flags & UNIT_BUF) == 0) {            /* not buffered? */
            rx_done (0, 0110);                          /* done, error */
            return IORETURN (rx_stopioe, SCPE_UNATT);
            }
        if (rx_track >= RX_NUMTR) {                     /* bad track? */
            rx_done (0, 0040);                          /* done, error */
            break;
            }
        uptr->TRACK = rx_track;                         /* now on track */
        if ((rx_sector == 0) || (rx_sector > RX_NUMSC)) { /* bad sect? */
            rx_done (0, 0070);                          /* done, error */
            break;
            }
        da = CALC_DA (rx_track, rx_sector);             /* get disk address */
        if (func == RXCS_WRDEL)                         /* del data? */
            rx_esr = rx_esr | RXES_DD;
        if (func == RXCS_READ) {                        /* read? */
            for (i = 0; i < RX_NUMBY; i++)
                rx_buf[i] = fbuf[da + i];
            }
        else {
            if (uptr->flags & UNIT_WPRT) {              /* write and locked? */
                rx_done (RXES_WLK, 0100);               /* done, error */
                break;
                }
            for (i = 0; i < RX_NUMBY; i++)              /* write */
                fbuf[da + i] = rx_buf[i];
            da = da + RX_NUMBY;
            if (da > uptr->hwmark)
                uptr->hwmark = da;
            }
        rx_done (0, 0);                                 /* done */
        break;

    case CMD_COMPLETE:                                  /* command complete */
        if (func == RXCS_ECODE) {                       /* read ecode? */
            rx_dbr = rx_ecode;                          /* set dbr */
            rx_done (0, -1);                            /* don't update */
            }
        else rx_done (0, 0);
        break;

    case INIT_COMPLETE:                                 /* init complete */
        rx_unit[0].TRACK = 1;                           /* drive 0 to trk 1 */
        rx_unit[1].TRACK = 0;                           /* drive 1 to trk 0 */
        if ((rx_unit[0].flags & UNIT_BUF) == 0) {       /* not buffered? */
            rx_done (RXES_ID, 0010);                    /* init done, error */
            break;
            }
        da = CALC_DA (1, 1);                            /* track 1, sector 1 */
        for (i = 0; i < RX_NUMBY; i++)                  /* read sector */
            rx_buf[i] = fbuf[da + i];
        rx_done (RXES_ID, 0);                           /* set done */
        if ((rx_unit[1].flags & UNIT_ATT) == 0)
            rx_ecode = 0020;
        break;
        }                                               /* end case state */

return SCPE_OK;
}

/* Command complete.  Set done and put final value in interface register,
   request interrupt if needed, return to IDLE state.
*/

void rx_done (int32 esr_flags, int32 new_ecode)
{
int32 drv = (rx_csr & RXCS_DRV)? 1: 0;

rx_state = IDLE;                                        /* now idle */
rx_csr = rx_csr | RXCS_DONE;                            /* set done */
if (rx_csr & RXCS_IE) SET_INT (RX);                     /* if ie, intr */
rx_esr = (rx_esr | esr_flags) & ~RXES_DRDY;
if (rx_unit[drv].flags & UNIT_ATT)
    rx_esr = rx_esr | RXES_DRDY;
if (new_ecode > 0)                                      /* test for error */
    rx_csr = rx_csr | RXCS_ERR;
if (new_ecode < 0)                                      /* don't update? */
    return;
rx_ecode = new_ecode;                                   /* update ecode */
rx_dbr = rx_esr;                                        /* update RXDB */
return;
}

/* Device initialization.  The RX is one of the few devices that schedules
   an I/O transfer as part of its initialization.
*/

t_stat rx_reset (DEVICE *dptr)
{
rx_csr = rx_dbr = 0;                                    /* clear regs */
rx_esr = rx_ecode = 0;                                  /* clear error */
rx_track = rx_sector = 0;                               /* clear addr */
rx_state = IDLE;                                        /* ctrl idle */
CLR_INT (RX);                                           /* clear int req */
sim_cancel (&rx_unit[1]);                               /* cancel drive 1 */
if (dptr->flags & DEV_DIS)                              /* disabled? */
    sim_cancel (&rx_unit[0]);
else if (rx_unit[0].flags & UNIT_BUF)  {                /* attached? */
    rx_state = INIT_COMPLETE;                           /* yes, sched init */
    sim_activate (&rx_unit[0], rx_swait * abs (1 - rx_unit[0].TRACK));
    }
else rx_done (0, 0010);                                 /* no, error */
return auto_config (0, 0);                              /* run autoconfig */
}

/* Device bootstrap */

#define BOOT_START      02000                           /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 026)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    042130,                         /* "XD" */
    0012706, BOOT_START,            /* MOV #boot_start, SP */
    0012700, 0000000,               /* MOV #unit, R0        ; unit number */
    0010003,                        /* MOV R0, R3 */
    0006303,                        /* ASL R3 */
    0006303,                        /* ASL R3 */
    0006303,                        /* ASL R3 */
    0006303,                        /* ASL R3 */
    0012701, 0177170,               /* MOV #RXCS, R1        ; csr */
    0032711, 0000040,               /* BITB #40, (R1)       ; ready? */
    0001775,                        /* BEQ .-4 */
    0052703, 0000007,               /* BIS #READ+GO, R3 */
    0010311,                        /* MOV R3, (R1)         ; read & go */
    0105711,                        /* TSTB (R1)            ; xfr ready? */
    0100376,                        /* BPL .-2 */
    0012761, 0000001, 0000002,      /* MOV #1, 2(R1)        ; sector */
    0105711,                        /* TSTB (R1)            ; xfr ready? */
    0100376,                        /* BPL .-2 */
    0012761, 0000001, 0000002,      /* MOV #1, 2(R1)        ; track */
    0005003,                        /* CLR R3 */
    0032711, 0000040,               /* BITB #40, (R1)       ; ready? */
    0001775,                        /* BEQ .-4 */
    0012711, 0000003,               /* MOV #EMPTY+GO, (R1)  ; empty & go */
    0105711,                        /* TSTB (R1)            ; xfr, done? */
    0001776,                        /* BEQ .-2 */
    0100003,                        /* BPL .+010 */
    0116123, 0000002,               /* MOVB 2(R1), (R3)+    ; move byte */
    0000772,                        /* BR .-012 */
    0005002,                        /* CLR R2 */
    0005003,                        /* CLR R3 */
    0012704, BOOT_START+020,        /* MOV #START+20, R4 */
    0005005,                        /* CLR R5 */
    0005007                         /* CLR R7 */
    };

t_stat rx_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

for (i = 0; i < BOOT_LEN; i++)
    WrMemW (BOOT_START + (2 * i), boot_rom[i]);
WrMemW (BOOT_UNIT, unitno & RX_M_NUMDR);
WrMemW (BOOT_CSR, rx_dib.ba & DMASK);
cpu_set_boot (BOOT_ENTRY);
return SCPE_OK;
}
