/* pdp11_rl.c: RL11 (RLV12) cartridge disk simulator

   Copyright (c) 1993-2008, Robert M Supnik

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

   rl           RL11(RLV12)/RL01/RL02 cartridge disk

   22-Sep-05    RMS     Fixed declarations (from Sterling Garwood)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   07-Jul-05    RMS     Removed extraneous externs
   30-Sep-04    RMS     Revised Unibus interface
   04-Jan-04    RMS     Changed sim_fsize calling sequence
   19-May-03    RMS     Revised for new conditional compilation scheme
   25-Apr-03    RMS     Revised for extended file support
   29-Sep-02    RMS     Added variable address support to bootstrap
                        Added vector change/display support
                        Revised mapping nomenclature
                        New data structures
   26-Jan-02    RMS     Revised bootstrap to conform to M9312
   06-Jan-02    RMS     Revised enable/disable support
   30-Nov-01    RMS     Added read only, extended SET/SHOW support
   26-Nov-01    RMS     Fixed per-drive error handling
   24-Nov-01    RMS     Converted FLG, CAPAC to arrays
   19-Nov-01    RMS     Fixed signed/unsigned mismatch in write check
   09-Nov-01    RMS     Added bus map, VAX support
   07-Sep-01    RMS     Revised device disable and interrupt mechanisms
   20-Aug-01    RMS     Added bad block option in attach
   17-Jul-01    RMS     Fixed warning from VC++ 6.0
   26-Apr-01    RMS     Added device enable/disable support
   25-Mar-01    RMS     Fixed block fill calculation
   15-Feb-01    RMS     Corrected bootstrap string
   12-Nov-97    RMS     Added bad block table command
   25-Nov-96    RMS     Default units to autosize
   29-Jun-96    RMS     Added unit disable support

   The RL11 is a four drive cartridge disk subsystem.  An RL01 drive
   consists of 256 cylinders, each with 2 surfaces containing 40 sectors
   of 256 bytes.  An RL02 drive has 512 cylinders.  The RLV12 is a
   controller variant which supports 22b direct addressing.

   The most complicated part of the RL11 controller is the way it does
   seeks.  Seeking is relative to the current disk address; this requires
   keeping accurate track of the current cylinder.  The RL11 will not
   switch heads or cross cylinders during transfers.

   The RL11 functions in three environments:

   - PDP-11 Q22 systems - the I/O map is one for one, so it's safe to
     go through the I/O map
   - PDP-11 Unibus 22b systems - the RL11 behaves as an 18b Unibus
     peripheral and must go through the I/O map
   - VAX Q22 systems - the RL11 must go through the I/O map
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "RL11 is not supported on the PDP-10!"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
extern uint32 cpu_opt;
#endif

/* Constants */

#define RL_NUMWD        128                             /* words/sector */
#define RL_NUMSC        40                              /* sectors/surface */
#define RL_NUMSF        2                               /* surfaces/cylinder */
#define RL_NUMCY        256                             /* cylinders/drive */
#define RL_NUMDR        4                               /* drives/controller */
#define RL_MAXFR        (1 << 16)                       /* max transfer */
#define RL01_SIZE (RL_NUMCY * RL_NUMSF * RL_NUMSC * RL_NUMWD)  /* words/drive */
#define RL02_SIZE       (RL01_SIZE * 2)                 /* words/drive */

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* hwre write lock */
#define UNIT_V_RL02     (UNIT_V_UF + 1)                 /* RL01 vs RL02 */
#define UNIT_V_AUTO     (UNIT_V_UF + 2)                 /* autosize enable */
#define UNIT_V_DUMMY    (UNIT_V_UF + 3)                 /* dummy flag */
#define UNIT_DUMMY      (1 << UNIT_V_DUMMY)
#define UNIT_WLK        (1u << UNIT_V_WLK)
#define UNIT_RL02       (1u << UNIT_V_RL02)
#define UNIT_AUTO       (1u << UNIT_V_AUTO)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protected */

/* Parameters in the unit descriptor */

#define TRK             u3                              /* current track */
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

/* RLCS */

#define RLCS_DRDY       0000001                         /* drive ready */
#define RLCS_M_FUNC     0000007                         /* function */
#define  RLCS_NOP       0
#define  RLCS_WCHK      1
#define  RLCS_GSTA      2
#define  RLCS_SEEK      3
#define  RLCS_RHDR      4
#define  RLCS_WRITE     5
#define  RLCS_READ      6
#define  RLCS_RNOHDR    7
#define RLCS_V_FUNC     1
#define RLCS_M_MEX      03                              /* memory extension */
#define RLCS_V_MEX      4
#define RLCS_MEX        (RLCS_M_MEX << RLCS_V_MEX)
#define RLCS_M_DRIVE    03
#define RLCS_V_DRIVE    8
#define RLCS_INCMP      0002000                         /* incomplete */
#define RLCS_CRC        0004000                         /* CRC error */
#define RLCS_HDE        0010000                         /* header error */
#define RLCS_NXM        0020000                         /* non-exist memory */
#define RLCS_DRE        0040000                         /* drive error */
#define RLCS_ERR        0100000                         /* error summary */
#define RLCS_ALLERR (RLCS_ERR+RLCS_DRE+RLCS_NXM+RLCS_HDE+RLCS_CRC+RLCS_INCMP)
#define RLCS_RW         0001776                         /* read/write */
#define GET_FUNC(x)     (((x) >> RLCS_V_FUNC) & RLCS_M_FUNC)
#define GET_DRIVE(x)    (((x) >> RLCS_V_DRIVE) & RLCS_M_DRIVE)

/* RLDA */

#define RLDA_SK_DIR     0000004                         /* direction */
#define RLDA_GS_CLR     0000010                         /* clear errors */
#define RLDA_SK_HD      0000020                         /* head select */

#define RLDA_V_SECT     0                               /* sector */
#define RLDA_M_SECT     077
#define RLDA_V_TRACK    6                               /* track */
#define RLDA_M_TRACK    01777
#define RLDA_HD0        (0 << RLDA_V_TRACK)
#define RLDA_HD1        (1u << RLDA_V_TRACK)
#define RLDA_V_CYL      7                               /* cylinder */
#define RLDA_M_CYL      0777
#define RLDA_TRACK      (RLDA_M_TRACK << RLDA_V_TRACK)
#define RLDA_CYL        (RLDA_M_CYL << RLDA_V_CYL)
#define GET_SECT(x)     (((x) >> RLDA_V_SECT) & RLDA_M_SECT)
#define GET_CYL(x)      (((x) >> RLDA_V_CYL) & RLDA_M_CYL)
#define GET_TRACK(x)    (((x) >> RLDA_V_TRACK) & RLDA_M_TRACK)
#define GET_DA(x)       ((GET_TRACK (x) * RL_NUMSC) + GET_SECT (x))

/* RLBA */

#define RLBA_IMP        0177776                         /* implemented */

/* RLBAE */

#define RLBAE_IMP       0000077                         /* implemented */

extern int32 int_req[IPL_HLVL];

uint16 *rlxb = NULL;                                    /* xfer buffer */
int32 rlcs = 0;                                         /* control/status */
int32 rlba = 0;                                         /* memory address */
int32 rlbae = 0;                                        /* mem addr extension */
int32 rlda = 0;                                         /* disk addr */
int32 rlmp = 0, rlmp1 = 0, rlmp2 = 0;                   /* mp register queue */
int32 rl_swait = 10;                                    /* seek wait */
int32 rl_rwait = 10;                                    /* rotate wait */
int32 rl_stopioe = 1;                                   /* stop on error */

DEVICE rl_dev;
t_stat rl_rd (int32 *data, int32 PA, int32 access);
t_stat rl_wr (int32 data, int32 PA, int32 access);
t_stat rl_svc (UNIT *uptr);
t_stat rl_reset (DEVICE *dptr);
void rl_set_done (int32 error);
t_stat rl_boot (int32 unitno, DEVICE *dptr);
t_stat rl_attach (UNIT *uptr, char *cptr);
t_stat rl_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat rl_set_bad (UNIT *uptr, int32 val, char *cptr, void *desc);

/* RL11 data structures

   rl_dev       RL device descriptor
   rl_unit      RL unit list
   rl_reg       RL register list
   rl_mod       RL modifier list
*/

DIB rl_dib = {
    IOBA_RL, IOLN_RL, &rl_rd, &rl_wr,
    1, IVCL (RL), VEC_RL, { NULL } };

UNIT rl_unit[] = {
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_AUTO, RL01_SIZE) },
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_AUTO, RL01_SIZE) },
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_AUTO, RL01_SIZE) },
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_AUTO, RL01_SIZE) }
    };

REG rl_reg[] = {
    { GRDATA (RLCS, rlcs, DEV_RDX, 16, 0) },
    { GRDATA (RLDA, rlda, DEV_RDX, 16, 0) },
    { GRDATA (RLBA, rlba, DEV_RDX, 16, 0) },
    { GRDATA (RLBAE, rlbae, DEV_RDX, 6, 0) },
    { GRDATA (RLMP, rlmp, DEV_RDX, 16, 0) },
    { GRDATA (RLMP1, rlmp1, DEV_RDX, 16, 0) },
    { GRDATA (RLMP2, rlmp2, DEV_RDX, 16, 0) },
    { FLDATA (INT, IREQ (RL), INT_V_RL) },
    { FLDATA (ERR, rlcs, CSR_V_ERR) },
    { FLDATA (DONE, rlcs, CSR_V_DONE) },
    { FLDATA (IE, rlcs, CSR_V_IE) },
    { DRDATA (STIME, rl_swait, 24), PV_LEFT },
    { DRDATA (RTIME, rl_rwait, 24), PV_LEFT },
    { URDATA (CAPAC, rl_unit[0].capac, 10, T_ADDR_W, 0,
              RL_NUMDR, PV_LEFT + REG_HRO) },
    { FLDATA (STOP_IOE, rl_stopioe, 0) },
    { GRDATA (DEVADDR, rl_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, rl_dib.vec, DEV_RDX, 16, 0), REG_HRO },
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
    { MTAB_XTD|MTAB_VDV, 010, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
      &set_vec, &show_vec, NULL },
    { 0 }
    };

DEVICE rl_dev = {
    "RL", rl_unit, rl_reg, rl_mod,
    RL_NUMDR, DEV_RDX, 24, 1, DEV_RDX, 16,
    NULL, NULL, &rl_reset,
    &rl_boot, &rl_attach, NULL,
    &rl_dib, DEV_DISABLE | DEV_UBUS | DEV_QBUS
    };

/* I/O dispatch routines, I/O addresses 17774400 - 17774407

   17774400     RLCS    read/write
   17774402     RLBA    read/write
   17774404     RLDA    read/write
   17774406     RLMP    read/write
   17774410     RLBAE   read/write
*/

t_stat rl_rd (int32 *data, int32 PA, int32 access)
{
UNIT *uptr;

switch ((PA >> 1) & 07) {                               /* decode PA<2:1> */

    case 0:                                             /* RLCS */
        rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
        if (rlcs & RLCS_ALLERR)
            rlcs = rlcs | RLCS_ERR;
        uptr = rl_dev.units + GET_DRIVE (rlcs);
        if (sim_is_active (uptr))
            rlcs = rlcs & ~RLCS_DRDY;
        else rlcs = rlcs | RLCS_DRDY;                   /* see if ready */
        *data = rlcs;
        break;

    case 1:                                             /* RLBA */
        *data = rlba & RLBA_IMP;
        break;

    case 2:                                             /* RLDA */
        *data = rlda;
        break;

    case 3:                                             /* RLMP */
        *data = rlmp;
        rlmp = rlmp1;                                   /* ripple data */
        rlmp1 = rlmp2;
        break;

    case 4:                                             /* RLBAE */
        if (UNIBUS)                                     /* not in RL11 */
            return SCPE_NXM;
        *data = rlbae & RLBAE_IMP;
        break;
        }                                               /* end switch */

return SCPE_OK;
}

t_stat rl_wr (int32 data, int32 PA, int32 access)
{
int32 curr, offs, newc, maxc;
UNIT *uptr;

switch ((PA >> 1) & 07) {                               /* decode PA<2:1> */

    case 0:                                             /* RLCS */
        rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
        if (rlcs & RLCS_ALLERR)
            rlcs = rlcs | RLCS_ERR;
        uptr = rl_dev.units + GET_DRIVE (data);         /* get new drive */
        if (sim_is_active (uptr))
            rlcs = rlcs & ~RLCS_DRDY;
        else rlcs = rlcs | RLCS_DRDY;                   /* see if ready */

        if (access == WRITEB)
            data = (PA & 1)? (rlcs & 0377) | (data << 8): (rlcs & ~0377) | data;
        rlcs = (rlcs & ~RLCS_RW) | (data & RLCS_RW);
        rlbae = (rlbae & ~RLCS_M_MEX) | ((rlcs >> RLCS_V_MEX) & RLCS_M_MEX);
        if (data & CSR_DONE) {                          /* ready set? */
            if ((data & CSR_IE) == 0)
                CLR_INT (RL);
            else if ((rlcs & (CSR_DONE + CSR_IE)) == CSR_DONE)
                SET_INT (RL);   
            return SCPE_OK;
            }

        CLR_INT (RL);                                   /* clear interrupt */
        rlcs = rlcs & ~RLCS_ALLERR;                     /* clear errors */
        switch (GET_FUNC (rlcs)) {                      /* case on RLCS<3:1> */
        case RLCS_NOP:                                  /* nop */
            rl_set_done (0);
            break;
        case RLCS_SEEK:                                 /* seek */
            curr = GET_CYL (uptr->TRK);                 /* current cylinder */
            offs = GET_CYL (rlda);                      /* offset */
            if (rlda & RLDA_SK_DIR) {                   /* in or out? */
                newc = curr + offs;                     /* out */
                maxc = (uptr->flags & UNIT_RL02)?
                    RL_NUMCY * 2: RL_NUMCY;
                if (newc >= maxc)
                    newc = maxc - 1;
                }
            else {
                newc = curr - offs;                     /* in */
                if (newc < 0)
                    newc = 0;
                }
            uptr->TRK = (newc << RLDA_V_CYL) |          /* put on track */
                ((rlda & RLDA_SK_HD)? RLDA_HD1: RLDA_HD0);
            sim_activate (uptr, rl_swait * abs (newc - curr));
            break;
        default:                                        /* data transfer */
            sim_activate (uptr, rl_swait);              /* activate unit */
            break;
            }                                           /* end switch func */
        break;                                          /* end case RLCS */

    case 1:                                             /* RLBA */
        if (access == WRITEB)
            data = (PA & 1)? (rlba & 0377) | (data << 8): (rlba & ~0377) | data;
        rlba = data & RLBA_IMP;
        break;

    case 2:                                             /* RLDA */
        if (access == WRITEB)
            data = (PA & 1)? (rlda & 0377) | (data << 8): (rlda & ~0377) | data;
        rlda = data;
        break;

    case 3:                                             /* RLMP */
        if (access == WRITEB)
            data = (PA & 1)? (rlmp & 0377) | (data << 8): (rlmp & ~0377) | data;
        rlmp = rlmp1 = rlmp2 = data;
        break;

    case 4:                                             /* RLBAE */
        if (UNIBUS)                                     /* not in RL11 */
            return SCPE_NXM;
        if (PA & 1)
            return SCPE_OK;
        rlbae = data & RLBAE_IMP;
        rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
        break;
        }                                               /* end switch */

return SCPE_OK;
}

/* Service unit timeout

   If seek in progress, complete seek command
   Else complete data transfer command

   The unit control block contains the function and cylinder for
   the current command.
*/

t_stat rl_svc (UNIT *uptr)
{
int32 err, wc, maxwc, t;
int32 i, func, da, awc;
uint32 ma;
uint16 comp;

func = GET_FUNC (rlcs);                                 /* get function */
if (func == RLCS_GSTA) {                                /* get status */
    if (rlda & RLDA_GS_CLR)
        uptr->STAT = uptr->STAT & ~RLDS_ERR;
    rlmp = uptr->STAT | (uptr->TRK & RLDS_HD) |
        ((uptr->flags & UNIT_ATT)? RLDS_ATT: RLDS_UNATT);
    if (uptr->flags & UNIT_RL02)
        rlmp = rlmp | RLDS_RL02;
    if (uptr->flags & UNIT_WPRT)
        rlmp = rlmp | RLDS_WLK;
    rlmp2 = rlmp1 = rlmp;
    rl_set_done (0);                                    /* done */
    return SCPE_OK;
    }

if ((uptr->flags & UNIT_ATT) == 0) {                    /* attached? */
    rlcs = rlcs & ~RLCS_DRDY;                           /* clear drive ready */
    uptr->STAT = uptr->STAT | RLDS_SPE;                 /* spin error */
    rl_set_done (RLCS_ERR | RLCS_INCMP);                /* flag error */
    return IORETURN (rl_stopioe, SCPE_UNATT);
    }

if ((func == RLCS_WRITE) && (uptr->flags & UNIT_WPRT)) {
    uptr->STAT = uptr->STAT | RLDS_WGE;                 /* write and locked */
    rl_set_done (RLCS_ERR | RLCS_DRE);
    return SCPE_OK;
    }

if (func == RLCS_SEEK) {                                /* seek? */
    rl_set_done (0);                                    /* done */
    return SCPE_OK;
    }

if (func == RLCS_RHDR) {                                /* read header? */
    rlmp = (uptr->TRK & RLDA_TRACK) | GET_SECT (rlda);
    rlmp1 = rlmp2 = 0;
    rl_set_done (0);                                    /* done */
    return SCPE_OK;
    }

if (((func != RLCS_RNOHDR) && ((uptr->TRK & RLDA_CYL) != (rlda & RLDA_CYL)))
   || (GET_SECT (rlda) >= RL_NUMSC)) {                  /* bad cyl or sector? */
    rl_set_done (RLCS_ERR | RLCS_HDE | RLCS_INCMP);     /* wrong cylinder? */
    return SCPE_OK;
    }
    
ma = (rlbae << 16) | rlba;                              /* get mem addr */
da = GET_DA (rlda) * RL_NUMWD;                          /* get disk addr */
wc = 0200000 - rlmp;                                    /* get true wc */

maxwc = (RL_NUMSC - GET_SECT (rlda)) * RL_NUMWD;        /* max transfer */
if (wc > maxwc)                                         /* track overrun? */
    wc = maxwc;
err = fseek (uptr->fileref, da * sizeof (int16), SEEK_SET);

if ((func >= RLCS_READ) && (err == 0)) {                /* read (no hdr)? */
    i = fxread (rlxb, sizeof (int16), wc, uptr->fileref);
    err = ferror (uptr->fileref);
    for ( ; i < wc; i++)                                /* fill buffer */
        rlxb[i] = 0;
    if (t = Map_WriteW (ma, wc << 1, rlxb)) {           /* store buffer */
        rlcs = rlcs | RLCS_ERR | RLCS_NXM;              /* nxm */
        wc = wc - t;                                    /* adjust wc */
        }
    }                                                   /* end read */

if ((func == RLCS_WRITE) && (err == 0)) {               /* write? */
    if (t = Map_ReadW (ma, wc << 1, rlxb)) {            /* fetch buffer */
        rlcs = rlcs | RLCS_ERR | RLCS_NXM;              /* nxm */
        wc = wc - t;                                    /* adj xfer lnt */
        }
    if (wc) {                                           /* any xfer? */
        awc = (wc + (RL_NUMWD - 1)) & ~(RL_NUMWD - 1);  /* clr to */
        for (i = wc; i < awc; i++)                      /* end of blk */
            rlxb[i] = 0;
        fxwrite (rlxb, sizeof (int16), awc, uptr->fileref);
        err = ferror (uptr->fileref);
        }
    }                                                   /* end write */

if ((func == RLCS_WCHK) && (err == 0)) {                /* write check? */
    i = fxread (rlxb, sizeof (int16), wc, uptr->fileref);
    err = ferror (uptr->fileref);
    for ( ; i < wc; i++)                                /* fill buffer */
        rlxb[i] = 0;
    awc = wc;                                           /* save wc */
    for (wc = 0; (err == 0) && (wc < awc); wc++)  {     /* loop thru buf */
        if (Map_ReadW (ma + (wc << 1), 2, &comp)) {     /* mem wd */
            rlcs = rlcs | RLCS_ERR | RLCS_NXM;          /* nxm */
            break;
            }
        if (comp != rlxb[wc])                           /* check to buf */
            rlcs = rlcs | RLCS_ERR | RLCS_CRC;
        }                                               /* end for */
    }                                                   /* end wcheck */

rlmp = (rlmp + wc) & 0177777;                           /* final word count */
if (rlmp != 0)                                          /* completed? */
    rlcs = rlcs | RLCS_ERR | RLCS_INCMP;
ma = ma + (wc << 1);                                    /* final byte addr */
rlbae = (ma >> 16) & RLBAE_IMP;                         /* upper 6b */
rlba = ma & RLBA_IMP;                                   /* lower 16b */
rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
rlda = rlda + ((wc + (RL_NUMWD - 1)) / RL_NUMWD);
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
rlcs = rlcs | status | CSR_DONE;                        /* set done */
if (rlcs & CSR_IE)
    SET_INT (RL);
else CLR_INT (RL);
return;
}

/* Device reset

   Note that the RL11 does NOT recalibrate its drives on RESET
*/

t_stat rl_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

rlcs = CSR_DONE;
rlda = rlba = rlbae = rlmp = rlmp1 = rlmp2 = 0;
CLR_INT (RL);
for (i = 0; i < RL_NUMDR; i++) {
    uptr = rl_dev.units + i;
    sim_cancel (uptr);
    uptr->STAT = 0;
    }
if (rlxb == NULL)
    rlxb = (uint16 *) calloc (RL_MAXFR, sizeof (uint16));
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
uptr->TRK = 0;                                          /* cylinder 0 */
uptr->STAT = RLDS_VCK;                                  /* new volume */
if ((p = sim_fsize (uptr->fileref)) == 0) {             /* new disk image? */
    if (uptr->flags & UNIT_RO)                          /* if ro, done */
        return SCPE_OK;
    return pdp11_bad_block (uptr, RL_NUMSC, RL_NUMWD);
    }
if ((uptr->flags & UNIT_AUTO) == 0)                     /* autosize? */
    return SCPE_OK;
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

/* Set bad block routine */

t_stat rl_set_bad (UNIT *uptr, int32 val, char *cptr, void *desc)
{
return pdp11_bad_block (uptr, RL_NUMSC, RL_NUMWD);
}

/* Device bootstrap */

#if defined (VM_PDP11)

#define BOOT_START      02000                           /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 020)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    0042114,                        /* "LD" */
    0012706, BOOT_START,            /* MOV #boot_start, SP */
    0012700, 0000000,               /* MOV #unit, R0 */
    0010003,                        /* MOV R0, R3 */
    0000303,                        /* SWAB R3 */
    0012701, 0174400,               /* MOV #RLCS, R1        ; csr */
    0012761, 0000013, 0000004,      /* MOV #13, 4(R1)       ; clr err */
    0052703, 0000004,               /* BIS #4, R3           ; unit+gstat */
    0010311,                        /* MOV R3, (R1)         ; issue cmd */
    0105711,                        /* TSTB (R1)            ; wait */
    0100376,                        /* BPL .-2 */
    0105003,                        /* CLRB R3 */
    0052703, 0000010,               /* BIS #10, R3          ; unit+rdhdr */
    0010311,                        /* MOV R3, (R1)         ; issue cmd */
    0105711,                        /* TSTB (R1)            ; wait */
    0100376,                        /* BPL .-2 */
    0016102, 0000006,               /* MOV 6(R1), R2        ; get hdr */
    0042702, 0000077,               /* BIC #77, R2          ; clr sector */
    0005202,                        /* INC R2               ; magic bit */
    0010261, 0000004,               /* MOV R2, 4(R1)        ; seek to 0 */
    0105003,                        /* CLRB R3 */
    0052703, 0000006,               /* BIS #6, R3           ; unit+seek */
    0010311,                        /* MOV R3, (R1)         ; issue cmd */
    0105711,                        /* TSTB (R1)            ; wait */
    0100376,                        /* BPL .-2 */
    0005061, 0000002,               /* CLR 2(R1)            ; clr ba */
    0005061, 0000004,               /* CLR 4(R1)            ; clr da */
    0012761, 0177000, 0000006,      /* MOV #-512., 6(R1)    ; set wc */
    0105003,                        /* CLRB R3 */
    0052703, 0000014,               /* BIS #14, R3          ; unit+read */
    0010311,                        /* MOV R3, (R1)         ; issue cmd */
    0105711,                        /* TSTB (R1)            ; wait */
    0100376,                        /* BPL .-2 */
    0042711, 0000377,               /* BIC #377, (R1) */
    0005002,                        /* CLR R2 */
    0005003,                        /* CLR R3 */
    0012704, BOOT_START+020,        /* MOV #START+20, R4 */
    0005005,                        /* CLR R5 */
    0005007                         /* CLR PC */
    };

t_stat rl_boot (int32 unitno, DEVICE *dptr)
{
int32 i;
extern uint16 *M;
extern int32 saved_PC;

for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & RLCS_M_DRIVE;
M[BOOT_CSR >> 1] = rl_dib.ba & DMASK;
saved_PC = BOOT_ENTRY;
return SCPE_OK;
}

#else

t_stat rl_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_NOFNC;
}

#endif

