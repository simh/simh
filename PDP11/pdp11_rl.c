/* pdp11_rl.c: RL11 (RLV12) cartridge disk simulator

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

   rl           RL11(RLV12)/RL01/RL02 cartridge disk

   23-Oct-13    RMS     Revised for new boot setup routine
   24-Mar-11    JAD     Various changes to support diagnostics, including:
                        - distinguish between RLV11 & 12
                        - more complete drive state
                        - improved head position tracking
                        - implement MAINT command of RLV11/12
                        - always respect unit disable flag
                        New commands added:
                        SHOW RLn DSTATE
                        SET RLn LOAD/UNLOAD
                        SET RLn OPEN/CLOSED
                        SET RLn BRUSH/NOBRUSH
                        SET RLn ONLINE/OFFLINE
                        SET RL RLV11/RLV12 (PDP-11 only)
                        SET RL DEBUG/NODEBUG
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
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
extern UNIT cpu_unit;
#endif

/* Constants */

#define RL_NUMWD        (128)                           /* words/sector */
#define RL_NUMSC        (40)                            /* sectors/surface */
#define RL_NUMSF        (2)                             /* surfaces/cylinder */
#define RL_NUMCY        (256)                           /* cylinders/drive */
#define RL_NUMDR        (4)                             /* drives/controller */
#define RL_MAXFR        (RL_NUMSC * RL_NUMWD)           /* max transfer */
#define RL01_SIZE (RL_NUMCY * RL_NUMSF * RL_NUMSC * RL_NUMWD) /* words/drive */
#define RL02_SIZE       (RL01_SIZE * 2)                 /* words/drive */

/* Device flags */

#define DEV_V_RLV11     (DEV_V_UF + 7)                  /* RLV11 */
#define DEV_RLV11       (1u << DEV_V_RLV11)

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* hwre write lock */
#define UNIT_V_RL02     (UNIT_V_UF + 1)                 /* RL01 vs RL02 */
#define UNIT_V_AUTO     (UNIT_V_UF + 2)                 /* autosize enable */
#define UNIT_V_DUMMY    (UNIT_V_UF + 3)                 /* dummy flag, for SET BADBLOCK */
#define UNIT_V_OFFL     (UNIT_V_UF + 4)                 /* unit off line */
#define UNIT_V_BRUSH    (UNIT_V_UF + 5)                 /* unit has brushes */
#define UNIT_BRUSH      (1u << UNIT_V_BRUSH)
#define UNIT_OFFL       (1u << UNIT_V_OFFL)
#define UNIT_DUMMY      (1u << UNIT_V_DUMMY)
#define UNIT_WLK        (1u << UNIT_V_WLK)
#define UNIT_RL02       (1u << UNIT_V_RL02)
#define UNIT_AUTO       (1u << UNIT_V_AUTO)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protected */

/* Parameters in the unit descriptor */

#define TRK             u3                              /* current track:head:sector */
#define STAT            u4                              /* status */
#define FNC             u5                              /* function */

/* RLDS, NI = not implemented, * = kept in STAT, ^ = kept in TRK , ! = kept in uptr */

#define RLDS_M_STATE    (07)
#define RLDS_LOAD       (0)                             /* no cartridge */
#define RLDS_SPIN       (1)                             /* spin-up */
#define RLDS_BRUSH      (2)                             /* brush cycle *! */
#define RLDS_HLOAD      (3)                             /* load heads */
#define RLDS_SEEK       (4)                             /* drive seeking * */
#define RLDS_LOCK       (5)                             /* lock on * */
#define RLDS_UNL        (6)                             /* unload heads */
#define RLDS_DOWN       (7)                             /* spin-down */
#define RLDS_BHO        (0000010)                       /* brushes home * */
#define RLDS_HDO        (0000020)                       /* heads out * */
#define RLDS_CVO        (0000040)                       /* cover open * */
#define RLDS_HD         (0000100)                       /* head select ^ */
#define RLDS_RL02       (0000200)                       /* RL02 ! */
#define RLDS_DSE        (0000400)                       /* drv sel err */
#define RLDS_VCK        (0001000)                       /* vol check * */
#define RLDS_WGE        (0002000)                       /* wr gate err * */
#define RLDS_SPE        (0004000)                       /* spin err * */
#define RLDS_STO        (0010000)                       /* seek time out * */
#define RLDS_WLK        (0020000)                       /* wr locked ! */
#define RLDS_HCE        (0040000)                       /* hd curr err NI */
#define RLDS_WDE        (0100000)                       /* wr data err NI */
#define RLDS_ERR        (RLDS_WDE|RLDS_HCE|RLDS_STO|RLDS_SPE|RLDS_WGE| \
                         RLDS_VCK|RLDS_DSE)             /* errors bits */

/* RLCS */

#define RLCS_DRDY       (0000001)                       /* drive ready */
#define RLCS_M_FUNC     (0000007)                       /* function */
#define  RLCS_NOP       (0)
#define  RLCS_WCHK      (1)
#define  RLCS_GSTA      (2)
#define  RLCS_SEEK      (3)
#define  RLCS_RHDR      (4)
#define  RLCS_WRITE     (5)
#define  RLCS_READ      (6)
#define  RLCS_RNOHDR    (7)
#define  RLCS_SPECIAL   (8)                             /* internal function, drive state */
#define RLCS_V_FUNC     (1)
#define RLCS_M_MEX      (03)                            /* memory extension */
#define RLCS_V_MEX      (4)
#define RLCS_MEX        (RLCS_M_MEX << RLCS_V_MEX)
#define RLCS_M_DRIVE    (03)
#define RLCS_V_DRIVE    (8)
#define RLCS_INCMP      (0002000)                       /* incomplete */
#define RLCS_CRC        (0004000)                       /* CRC error */
#define RLCS_HCRC       (RLCS_CRC|RLCS_INCMP)           /* header CRC error */
#define RLCS_DLT        (0010000)                       /* data late */
#define RLCS_HDE        (RLCS_DLT|RLCS_INCMP)           /* header not found error */
#define RLCS_NXM        (0020000)                       /* non-exist memory */
#define RLCS_PAR        (RLCS_NXM|RLCS_INCMP)           /* parity error */
#define RLCS_DRE        (0040000)                       /* drive error */
#define RLCS_ERR        (0100000)                       /* error summary */
#define RLCS_ALLERR     (RLCS_ERR|RLCS_DRE|RLCS_NXM|RLCS_HDE|RLCS_CRC|RLCS_INCMP)
#define RLCS_RW         (0001776)                       /* read/write */
#define GET_FUNC(x)     (((x) >> RLCS_V_FUNC) & RLCS_M_FUNC)
#define GET_DRIVE(x)    (((x) >> RLCS_V_DRIVE) & RLCS_M_DRIVE)

/* RLDA */

#define RLDA_GS         (0000002)                       /* get status */
#define RLDA_SK_DIR     (0000004)                       /* direction */
#define RLDA_GS_CLR     (0000010)                       /* clear errors */
#define RLDA_SK_HD      (0000020)                       /* head select */

#define RLDA_V_SECT     (0)                             /* sector */
#define RLDA_M_SECT     (077)
#define RLDA_V_TRACK    (6)                             /* track */
#define RLDA_M_TRACK    (01777)
#define RLDA_HD0        (0 << RLDA_V_TRACK)
#define RLDA_HD1        (1u << RLDA_V_TRACK)
#define RLDA_V_CYL      (7)                             /* cylinder */
#define RLDA_M_CYL      (0777)
#define RLDA_TRACK      (RLDA_M_TRACK << RLDA_V_TRACK)
#define RLDA_CYL        (RLDA_M_CYL << RLDA_V_CYL)
#define GET_SECT(x)     (((x) >> RLDA_V_SECT) & RLDA_M_SECT)
#define GET_CYL(x)      (((x) >> RLDA_V_CYL) & RLDA_M_CYL)
#define GET_TRACK(x)    (((x) >> RLDA_V_TRACK) & RLDA_M_TRACK)
#define GET_DA(x)       ((GET_TRACK (x) * RL_NUMSC) + GET_SECT (x))

/* RLBA */

#define RLBA_IMP        (0177777)                       /* implemented */

/* RLBAE */

#define RLBAE_IMP       (0000077)                       /* implemented */

extern int32 int_req[IPL_HLVL];

uint16 *rlxb = NULL;                                    /* xfer buffer */
int32 rlcs = 0;                                         /* control/status */
int32 rlba = 0;                                         /* memory address */
int32 rlbae = 0;                                        /* mem addr extension */
int32 rlda = 0;                                         /* disk addr */
uint16 rlmp = 0, rlmp1 = 0, rlmp2 = 0;                  /* mp register queue */
int32 rl_swait = 10;                                    /* seek wait */
int32 rl_rwait = 10;                                    /* rotate wait */
int32 rl_stopioe = 1;                                   /* stop on error */

/* forward references */
t_stat rl_rd (int32 *data, int32 PA, int32 access);
t_stat rl_wr (int32 data, int32 PA, int32 access);
t_stat rl_svc (UNIT *uptr);
t_stat rl_reset (DEVICE *dptr);
void rl_set_done (int32 error);
t_stat rl_boot (int32 unitno, DEVICE *dptr);
t_stat rl_attach (UNIT *uptr, CONST char *cptr);
t_stat rl_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rl_set_bad (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static void rlv_maint (void);
t_stat rl_detach (UNIT *uptr);
t_stat rl_set_cover (UNIT *, int32, CONST char *, void *);
t_stat rl_show_cover (FILE *, UNIT *, int32, CONST void *);
t_stat rl_set_load (UNIT *, int32, CONST char *, void *);
t_stat rl_show_load (FILE *, UNIT *, int32, CONST void *);
t_stat rl_show_dstate (FILE *, UNIT *, int32, CONST void *);
#if defined (VM_PDP11)
t_stat rl_set_ctrl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
#endif
t_stat rl_show_ctrl (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat rl_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *rl_description (DEVICE *dptr);

/* RL11 data structures

   rl_dev   RL device descriptor
   rl_unit  RL unit list
   rl_reg   RL register list
   rl_mod   RL modifier list
*/

#define IOLN_RL         012

static DIB rl_dib = {
    IOBA_AUTO, IOLN_RL, &rl_rd, &rl_wr,
    1, IVCL (RL), VEC_AUTO, { NULL }, IOLN_RL };

static UNIT rl_unit[] = {
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE+UNIT_AUTO, RL01_SIZE) },
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE+UNIT_AUTO, RL01_SIZE) },
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE+UNIT_AUTO, RL01_SIZE) },
    { UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE+UNIT_AUTO, RL01_SIZE) }
    };

static const REG rl_reg[] = {
    { GRDATAD (RLCS,              rlcs, DEV_RDX, 16, 0, "control/status") },
    { GRDATAD (RLDA,              rlda, DEV_RDX, 16, 0, "disk address") },
    { GRDATAD (RLBA,              rlba, DEV_RDX, 16, 0, "memory address") },
    { GRDATAD (RLBAE,            rlbae, DEV_RDX,  6, 0, "memory address extension (RLV12)") },
    { GRDATAD (RLMP,              rlmp, DEV_RDX, 16, 0, "multipurpose register queue") },
    { GRDATAD (RLMP1,            rlmp1, DEV_RDX, 16, 0, "multipurpose register queue") },
    { GRDATAD (RLMP2,            rlmp2, DEV_RDX, 16, 0, "multipurpose register queue") },
    { FLDATAD (INT,          IREQ (RL), INT_V_RL,       "interrupt pending flag") },
    { FLDATAD (ERR,               rlcs, CSR_V_ERR,      "error flag (CSR<15>)") },
    { FLDATAD (DONE,              rlcs, CSR_V_DONE,     "device done flag (CSR<7>)") },
    { FLDATAD (IE,                rlcs, CSR_V_IE,       "interrupt enable flag (CSR<6>)") },
    { DRDATAD (STIME,         rl_swait, 24,             "seek time, per cylinder"), PV_LEFT },
    { DRDATAD (RTIME,         rl_rwait, 24,             "rotational delay"), PV_LEFT },
    { URDATA  (CAPAC, rl_unit[0].capac, 10, T_ADDR_W, 0,
              RL_NUMDR, PV_LEFT + REG_HRO) },
    { FLDATAD (STOP_IOE,    rl_stopioe, 0,              "stop on I/O error flag") },
    { GRDATA  (DEVADDR, rl_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA  (DEVVEC, rl_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

static const MTAB rl_mod[] = {
#if defined (VM_PDP11)
    { MTAB_XTD|MTAB_VDV, (DEV_RLV11|DEV_Q18), "",   "RLV11", 
        &rl_set_ctrl, &rl_show_ctrl, NULL, "Set controller type RLV11" },
    { MTAB_XTD|MTAB_VDV,                   0, NULL, "RLV12", 
        &rl_set_ctrl, NULL,          NULL, "Set controller type RLV12" },
#endif
    { UNIT_OFFL,         0, "on line",  "ONLINE", 
        NULL, NULL, NULL, "Set unit online" },
    { UNIT_OFFL, UNIT_OFFL, "off line", "OFFLINE", 
        NULL, NULL, NULL, "Set unit offline" },
    { UNIT_BRUSH,          0, NULL,          "NOBRUSH",
        NULL, NULL, NULL, "Disable brushes" },
    { UNIT_BRUSH, UNIT_BRUSH, "has brushes", "BRUSH",
        NULL, NULL, NULL, "Enable brushes" },

    { MTAB_XTD|MTAB_VUN|MTAB_NMO, RLDS_CVO, "open",   "OPEN", 
        &rl_set_cover, &rl_show_cover, NULL, "Drive cover" },
    { MTAB_XTD|MTAB_VUN,                 0, NULL,     "CLOSED", 
        &rl_set_cover, NULL,           NULL, "Close drive cover" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO,        0, "load",   "LOAD", 
        &rl_set_load, &rl_show_load,   NULL, "Load drive" },
    { MTAB_XTD|MTAB_VUN,                 1, NULL,     "UNLOAD",
        &rl_set_load, NULL,            NULL, "Unload drive" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO,        0, "DSTATE", NULL, 
        NULL, &rl_show_dstate, NULL, "Display drive state" },
    { UNIT_WLK,        0, "write enabled", "WRITEENABLED", 
        NULL, NULL, NULL, "Write enable disk drive" },
    { UNIT_WLK, UNIT_WLK, "write locked",  "LOCKED", 
        NULL, NULL, NULL, "Write lock disk drive"  },
    { UNIT_DUMMY, 0, NULL, "BADBLOCK", 
        &rl_set_bad, NULL, NULL, "Write bad block table on last track" },
    { (UNIT_RL02+UNIT_ATT), UNIT_ATT, "RL01", NULL, NULL },
    { (UNIT_RL02+UNIT_ATT), (UNIT_RL02+UNIT_ATT), "RL02", NULL, NULL },
    { (UNIT_AUTO+UNIT_RL02+UNIT_ATT),         0, "RL01", NULL, 
        NULL, NULL, NULL, "Set drive type RL01" },
    { (UNIT_AUTO+UNIT_RL02+UNIT_ATT), UNIT_RL02, "RL02", NULL, 
        NULL, NULL, NULL, "Set drive type RL02" },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL,       NULL },
    { UNIT_AUTO,            UNIT_AUTO, NULL,       "AUTOSIZE", 
        NULL, NULL, NULL, "set type based on file size at ATTACH" },
    { (UNIT_AUTO+UNIT_RL02),         0, NULL, "RL01", 
        &rl_set_size, NULL, NULL, "Set drive type RL01" },
    { (UNIT_AUTO+UNIT_RL02), UNIT_RL02, NULL, "RL02", 
        &rl_set_size, NULL, NULL, "Set drive type RL02"  },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 010, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 }
    };

DEVICE rl_dev = {
    "RL", (UNIT *) &rl_unit, (REG *)rl_reg, (MTAB *)rl_mod,
    RL_NUMDR, DEV_RDX, 24, 1, DEV_RDX, 16,
    NULL, NULL, &rl_reset,
    &rl_boot, &rl_attach, &rl_detach,
    &rl_dib, DEV_DISABLE | DEV_UBUS | DEV_QBUS | DEV_DEBUG, 0,
    NULL, NULL, NULL, &rl_help, NULL, NULL,
    &rl_description 
    };

/* Drive states */
static const char * const state[] = {
    "Load Cartridge", "Spin Up", "Brush", "Load Heads",
    "Seek", "Lock On", "Unload Heads", "Spin Down"
};

/* I/O dispatch routines, I/O addresses 17774400 - 17774411

   17774400 RLCS    read/write
   17774402 RLBA    read/write
   17774404 RLDA    read/write
   17774406 RLMP    read/write
   17774410 RLBAE   read/write
*/

t_stat rl_rd (int32 *data, int32 PA, int32 access)
{
UNIT *uptr;

switch ((PA >> 1) & 07) {                               /* decode PA<2:1> */

    case 0:                                             /* RLCS */
    rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
/*
The DRDY signal is sent by the selected drive to indicate that it
is ready to read or write or seek.  It is sent when the heads are
not moving and are locked onto a cylinder.  This is continuously
monitored by the drive and controller. [EK-0RL11-TD-001, p. 3-8]
Use the DS bits to determine if the drive has any outstanding I/O
operations and set DRDY as appropriate.

This seems to imply that only a Seek operation (not Read/Write)
causes ready to be false.
*/
    uptr = rl_dev.units + GET_DRIVE (rlcs);
    if ((uptr->flags & UNIT_OFFL) || (uptr->STAT & RLDS_VCK)) {
        rlcs |= RLCS_DRE;
        rlcs &= ~RLCS_DRDY;
    } else if (sim_is_active (uptr) || (uptr->flags & UNIT_DIS) ||
        ((uptr->STAT & RLDS_M_STATE) != RLDS_LOCK))
        rlcs &= ~RLCS_DRDY;
    else
        rlcs |= RLCS_DRDY;                              /* see if ready */
/*
Make sure the error summary bit properly reflects the sum of other
errors.
*/
    if (rlcs & RLCS_ALLERR)
        rlcs |= RLCS_ERR;
    *data = rlcs;
    if (DEBUG_PRS (rl_dev))
        fprintf (sim_deb, ">>RL rd: RLCS %06o\n", rlcs);
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
        if (UNIBUS || (rl_dev.flags & DEV_RLV11))       /* not in RL11/RLV11 */
            return SCPE_NXM;
        *data = rlbae & RLBAE_IMP;
        break;

    default:
        return (SCPE_NXM);
    }                                                   /* end switch */

return SCPE_OK;
}

t_stat rl_wr (int32 data, int32 PA, int32 access)
{
int32 curr, offs, newc, maxc, tim;
UNIT *uptr;

switch ((PA >> 1) & 07) {                               /* decode PA<2:1> */

    case 0:                                             /* RLCS */
        rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
        uptr = rl_dev.units + GET_DRIVE (data);         /* get new drive */
        if (access == WRITEB)
            data = (PA & 1)? (rlcs & 0377) | (data << 8): (rlcs & ~0377) | data;
        if (DEBUG_PRS (rl_dev))
            fprintf (sim_deb, ">>RL wr: RLCS %06o new %06o\n", rlcs, data);
        rlcs = (rlcs & ~RLCS_RW) | (data & RLCS_RW);
        rlbae = (rlbae & ~RLCS_M_MEX) | ((rlcs >> RLCS_V_MEX) & RLCS_M_MEX);
/*
Commands to the controller are only executed with the CRDY (DONE)
bit is cleared by software.  If set, check for interrupts and return.
*/
    if (data & CSR_DONE) {                              /* ready set? */
        if ((data & CSR_IE) == 0)
            CLR_INT (RL);
        else if ((rlcs & (CSR_DONE + CSR_IE)) == CSR_DONE)
            SET_INT (RL);   
        return SCPE_OK;
        }

        CLR_INT (RL);                                   /* clear interrupt */
        rlcs &= ~RLCS_ALLERR;                           /* clear errors */
        switch (GET_FUNC (rlcs)) {                      /* case on RLCS<3:1> */
        case RLCS_NOP:                                  /* nop */
            if (!UNIBUS)                                /* RLV1x has MAINT command */
                rlv_maint ();
            rl_set_done (0);
            break;
        case RLCS_SEEK:                                 /* seek */
            if ((uptr->flags & (UNIT_DIS|UNIT_OFFL)) || (!(uptr->flags & UNIT_ATT))) {
                rl_set_done (RLCS_ERR | RLCS_INCMP);
                uptr->STAT |= RLDS_STO;
                break;
            }
            curr = GET_CYL (uptr->TRK);             /* current cylinder */
            offs = GET_CYL (rlda);                  /* offset */
            if (rlda & RLDA_SK_DIR) {               /* in or out? */
                newc = curr + offs;                 /* out */
                maxc = (uptr->flags & UNIT_RL02)?
                    RL_NUMCY * 2: RL_NUMCY;
                if (newc >= maxc)
                    newc = maxc - 1;
            } else {
                newc = curr - offs;                 /* in */
                if (newc < 0)
                    newc = 0;
            }
            /* enter velocity mode? only if a different cylinder */
            if (newc != curr)
            uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_SEEK; /* move the positioner */
/* TBD: if a head switch, sector should be RL_NUMSC/2? */
            uptr->TRK = (newc << RLDA_V_CYL) |      /* put on track */
                ((rlda & RLDA_SK_HD)? RLDA_HD1: RLDA_HD0);
/*
Real timing:
min 6.5ms, max 15ms for head switch,
max 17ms for 1 track seek w/head switch
55ms avg seek
100ms max seek
*/
            tim = abs (newc - curr);
            if (tim == 0)
                tim++;
            tim *= rl_swait;
            if (DEBUG_PRS (rl_dev))
                fprintf (sim_deb, ">>RL SEEK: drv %d, dist %d, head sw %d, tim %d\n",
                    (int32) (uptr - rl_dev.units),
                    abs (newc - curr), (rlda & RLDA_SK_HD), tim);
            uptr->FNC = RLCS_SEEK;
            sim_activate (uptr, tim);               /* must be > 0 */
            rl_set_done (0);                        /* ctrlr is ready */
            break;
        case RLCS_GSTA:
            if (!(rlda & RLDA_GS)) {                /* GS bit must be set */
                rl_set_done (RLCS_ERR | RLCS_INCMP);    /* OPI; request error */
                return (SCPE_OK);
            }
            if (rlda & RLDA_GS_CLR)                 /* reset errors? */
                uptr->STAT &= ~RLDS_ERR;
                /* develop drive state */
            rlmp = (uint16)(uptr->STAT | (uptr->TRK & RLDS_HD));
            if (uptr->flags & UNIT_RL02)
                rlmp |= RLDS_RL02;
            if (uptr->flags & UNIT_WPRT)
                rlmp |= RLDS_WLK;
            if (uptr->flags & (UNIT_DIS | UNIT_OFFL)) {
                rlmp |= RLDS_DSE;
                rl_set_done (RLCS_DRE | RLCS_INCMP);
            }
            rlmp2 = rlmp1 = rlmp;
            if (DEBUG_PRS (rl_dev))
                fprintf (sim_deb, ">>RL GSTA: rlds=%06o drv=%ld\n",
                    rlmp, (long)(uptr - rl_dev.units));
            rl_set_done (0);                        /* done */
            break;
        default:                                    /* data transfer */
            if ((uptr->flags & (UNIT_DIS|UNIT_OFFL)) || (!(uptr->flags & UNIT_ATT))) {
                rl_set_done (RLCS_INCMP);
                break;
                }
/*
EK-0RL11-TD-001, p2-3: "If the CPU software initiates another
operation on a drive that is busy seeking, the controller will
suspend the operation until the seek is completed."

Check for the condition where there is an outstanding operation but
the program is requesting another operation without waiting for
drive ready.  If so, remove the previous queue entry, complete the
operation now, and queue the next operation.
*/
            if (sim_is_active (uptr)) {
                sim_cancel (uptr);
                rl_svc (uptr);
                }
            uptr->FNC = GET_FUNC (rlcs);
            sim_activate (uptr, rl_swait);              /* activate unit */
            break;
            }                                           /* end switch func */
        break;                                          /* end case RLCS */
/*
Contrary to what the RL01/RL02 User Guide (EK-RL012-UG-006, p.4-5)
says, bit 0 can be written and read (as 1) on an RLV12 (verified
2011-01-05).  Not sure about the RLV11.
*/
    case 1:                                             /* RLBA */
        if (access == WRITEB)
            data = (PA & 1)? (rlba & 0377) | (data << 8): (rlba & ~0377) | data;
        rlba = data & (UNIBUS ? 0177776 : 0177777);
        if (DEBUG_PRS (rl_dev))
            fprintf (sim_deb, ">>RL wr: RLBA %06o\n", rlba);
        break;

    case 2:                                             /* RLDA */
        if (access == WRITEB)
            data = (PA & 1)? (rlda & 0377) | (data << 8): (rlda & ~0377) | data;
        rlda = data;
        if (DEBUG_PRS (rl_dev))
            fprintf (sim_deb, ">>RL wr: RLDA %06o\n", rlda);
        break;

    case 3:                                             /* RLMP */
        if (access == WRITEB)
            data = (PA & 1)? (rlmp & 0377) | (data << 8): (rlmp & ~0377) | data;
        rlmp = rlmp1 = rlmp2 = (uint16)data;
        if (DEBUG_PRS (rl_dev))
            fprintf (sim_deb, ">>RL wr: RLMP %06o\n", rlmp);
        break;

    case 4:                                             /* RLBAE */
        if (UNIBUS || (rl_dev.flags & DEV_RLV11))       /* not in RL11/RLV11 */
            return SCPE_NXM;
        if (PA & 1)
            return SCPE_OK;
        rlbae = data & RLBAE_IMP;
        rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
        if (DEBUG_PRS (rl_dev))
            fprintf (sim_deb, ">>RL wr: RLBAE %06o\n", rlbae);
        break;
    default:
        return (SCPE_NXM);
        }                                               /* end switch */

return SCPE_OK;
}

/* CRC16 as implemented by the DEC 9401 chip */
static uint16 calcCRC (const int wc, const uint16 *data)
{
    uint32  crc, j, d;
    int32   i;

    crc = 0;
    for (i = 0; i < wc; i++) {
        d = *data++;
        /* cribbed from KG11-A */
        for (j = 0; j < 16; j++) {
            crc = (crc & ~01) | ((crc & 01) ^ (d & 01));
            crc = (crc & 01) ? (crc >> 1) ^ 0120001 : crc >> 1;
            d >>= 1;
        }
    }
    return (uint16)crc;
}

/*
Perform the maintenance function of the RLV1x; this is fully described
on pages 4-14 and 4-15 of EK-RL012-UG-006.  Note that the description
of this in EK-RLV12-UG-002 (p.5-3) contains a typo, the constant
for -511 is incorrect.
*/
static void rlv_maint (void)
{
    int32   i;
    uint32  ma;
    uint16  w;

    if (DEBUG_PRS (rl_dev))
        fprintf (sim_deb, ">>RL maint: RLDA %06o\n", rlda);
    /* 1: check internal logic */
    rlda = (rlda & ~0377) | ((rlda + 1) & 0377);

    /* 2: check internal logic */
    rlda = (rlda & ~0377) | ((rlda + 1) & 0377);

    /* 3: check DMA transfers */
    ma = (rlbae << 16) | rlba;                          /* get mem addr */
    /* xfer 256 words to FIFO */
    if (DEBUG_PRS (rl_dev))
        fprintf (sim_deb, ">>RL maint: RLMP %06o\n", rlmp);
    if (rlmp != 0177001) {                              /* must be exactly -511 */
        rlcs |= RLCS_ERR | RLCS_HDE;                    /* HNF error */
        return;
    }
    for (i = 0; i < 256; i++) {
        if (Map_ReadW (ma, 2, &rlxb[i])) {              /* mem wd */
            rlcs |= RLCS_ERR | RLCS_NXM;                /* nxm */
            break;
        }
        ma += 2;
        rlmp++;
    }
    /* xfer 255 words from FIFO */
    for (i = 0; i < 255; i++) {
        if (Map_WriteW (ma, 2, &rlxb[i])) {             /* store buffer */
            rlcs |= RLCS_ERR | RLCS_NXM;                /* nxm */
            break;
        }
        ma += 2;
        rlmp++;
    }
    rlda = (rlda & ~0377) | ((rlda + 1) & 0377);
    rlbae = (ma >> 16) & RLBAE_IMP;                     /* upper 6b */
    rlba = ma & RLBA_IMP;                               /* lower 16b */

    /* 4: check the CRC of (DAR + 3) */
    w = (uint16)rlda;
    rlxb[0] = calcCRC (1, &w);                          /* calculate CRC */
    rlda = (rlda & ~0377) | ((rlda + 1) & 0377);

    /* 5: check the CRC of (DAR + 4) */
    w = (uint16)rlda;
    rlxb[1] = calcCRC (1, &w);                          /* calculate CRC */
    rlda = (rlda & ~0377) | ((rlda + 1) & 0377);

    /* 6: check the CRC of (CRC of DAR + 4) */
    w = rlxb[1];
    rlxb[1] = calcCRC (1, &w);                          /* calculate CRC */
    rlmp = rlxb[0];
    rlmp1 = rlxb[1];
    rlda = (rlda & ~0377) | ((rlda + 1) & 0377);
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
int32 i, da, awc;
uint32 ma;
uint16 comp;
static const char * const funcname[] = {
    "NOP", "WCK", "GSTA", "SEEK",
    "RHDR", "WT", "RD", "RNOHDR", "SPECIAL",
};

if (DEBUG_PRS (rl_dev)) {
    if (uptr->FNC == RLCS_SPECIAL)
        fprintf (sim_deb, ">>RL svc: func=SPECIAL(%s) drv=%d\n",
            state[uptr->STAT & RLDS_M_STATE], (int32) (uptr - rl_dev.units));
    else
        fprintf (sim_deb, ">>RL svc: func=%s drv=%d rlda=%06o\n",
            funcname[uptr->FNC], (int32) (uptr - rl_dev.units), rlda);
}

/* really shouldn't happen... */
if ((uptr->FNC == RLCS_GSTA) || (uptr->FNC == RLCS_NOP)) {
    rl_set_done (0);
    return (SCPE_OK);
    }

/*
This situation occurs when the drive (not controller) state needs
to transition from one state to another.  The state bits indicate
the state the drive is currently in.
*/

if (uptr->FNC == RLCS_SPECIAL) {
    switch (uptr->STAT & RLDS_M_STATE) {
/*
The LOAD state is a little different.  We can stay in LOAD until
the user hits the RUN (LOAD) button, at which time we should come
here to transition to the next state and begin the startup process.
*/
    case RLDS_LOAD:
        /* load pressed, spinning up */
        if (!(uptr->STAT & RLDS_CVO)) {
            uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_SPIN;
            /* actual time is 45-50 seconds from press to Lock */
            sim_activate (uptr, 200 * rl_swait);
            uptr->STAT &= ~RLDS_HDO;
            uptr->STAT |= RLDS_BHO;
        }
        break;
/*
Original RL01 drives would transition to the Brush Cycle, but this
was removed in a later ECO.
*/
    case RLDS_SPIN:     /* spun up, load brushes or heads */
        if (uptr->flags & UNIT_BRUSH) {
            uptr->STAT &= ~RLDS_BHO;
            uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_BRUSH;
        } else {
            uptr->STAT |= RLDS_BHO;
            uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_HLOAD;
        }
        sim_activate (uptr, 200 * rl_swait);
        break;
    case RLDS_BRUSH:
        uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_HLOAD;
        uptr->STAT |= RLDS_BHO;
        sim_activate (uptr, 200 * rl_swait);
        break;
    case RLDS_HLOAD:    /* heads loaded, seek to home */
        uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_SEEK;
        sim_activate (uptr, 200 * rl_swait);
        uptr->STAT |= RLDS_BHO | RLDS_HDO;
        uptr->TRK = 0;
        break;
    case RLDS_SEEK:     /* home found, lock on */
        /* enter postion mode */
        uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_LOCK;
        /* sim_activate (uptr, rl_swait); */
        break;
    case RLDS_LOCK:     /* tracking, nothing to do */
        /* illuminate ready lamp */
        break;
/*
Initiated by depressing the Run (LOAD) switch.
*/
    case RLDS_UNL:      /* unload pressed, heads unloaded, spin down */
        uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_DOWN;
        uptr->STAT &= ~RLDS_HDO;    /* retract heads */
        /* actual time is ~30 seconds */
        sim_activate (uptr, 200 * rl_swait);
        break;
    case RLDS_DOWN:     /* OK to open cover */
        uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_LOAD;
        uptr->STAT |= RLDS_BHO | RLDS_VCK;
        break;
    default:
        ; /* can't happen */
    }
    return (SCPE_OK);
}

if ((uptr->flags & UNIT_ATT) == 0) {                    /* attached? */
    uptr->STAT |= RLDS_SPE;                             /* spin error */
    rl_set_done (RLCS_ERR | RLCS_INCMP);                /* flag error */
    return IORETURN (rl_stopioe, SCPE_UNATT);
    }

if ((uptr->FNC == RLCS_WRITE) && (uptr->flags & UNIT_WPRT)) {
    uptr->STAT |= RLDS_WGE;                             /* write and locked */
    rl_set_done (RLCS_ERR | RLCS_DRE);
    return SCPE_OK;
    }

if (uptr->FNC == RLCS_SEEK) {                           /* seek? */
    /* enter position mode */
    uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_LOCK; /* heads locked on cyl */
    return (SCPE_OK);
    }

if (uptr->FNC == RLCS_RHDR) {                           /* read header? */
    uint16 hdr[2];
    hdr[0] = rlmp = uptr->TRK & 0177777;
    hdr[1] = rlmp1 = 0;
    rlmp2 = calcCRC (2, &hdr[0]);                       /* calculate header CRC */
    rl_set_done (0);                                    /* done */
    /* simulate sequential rotation about the current track */
    uptr->TRK = (uptr->TRK & ~RLDA_M_SECT) |
        ((uptr->TRK + 1) & RLDA_M_SECT);
    if (GET_SECT (uptr->TRK) >= RL_NUMSC)               /* end of track? */
        uptr->TRK &= ~RLDA_M_SECT;                      /* wrap to 0 */
    return (SCPE_OK);
    }

if (uptr->FNC == RLCS_RNOHDR) {
    if (GET_SECT (uptr->TRK) >= RL_NUMSC) {
        rl_set_done (RLCS_ERR | RLCS_HDE);              /* wrong cylinder? */
        return (SCPE_OK);
        }
    da = GET_DA (uptr->TRK) * RL_NUMWD;                 /* get disk addr */
    maxwc = (RL_NUMSC - GET_SECT (uptr->TRK)) * RL_NUMWD; /* max transfer */
} else {
    /* bad cyl or sector? */
    if (((uptr->TRK & RLDA_CYL) != (rlda & RLDA_CYL)) || (GET_SECT (rlda) >= RL_NUMSC)) {
        rl_set_done (RLCS_ERR | RLCS_HDE | RLCS_INCMP); /* wrong cylinder? */
        return (SCPE_OK);
        }
    da = GET_DA (rlda) * RL_NUMWD;                      /* get disk addr */
    maxwc = (RL_NUMSC - GET_SECT (rlda)) * RL_NUMWD;    /* max transfer */
}
    
ma = (rlbae << 16) | rlba;                              /* get mem addr */
wc = 0200000 - rlmp;                                    /* get true wc */

if (wc > maxwc)                                         /* track overrun? */
    wc = maxwc;
err = fseek (uptr->fileref, da * sizeof (int16), SEEK_SET);

if (DEBUG_PRS (rl_dev))
    fprintf (sim_deb, ">>RL svc: cyl %d, sect %d, wc %d, maxwc %d, err %d\n",
    GET_CYL (rlda), GET_SECT (rlda), wc, maxwc, err);

    if ((uptr->FNC >= RLCS_READ) && (err == 0)) {       /* read (no hdr)? */
        i = fxread (rlxb, sizeof (int16), wc, uptr->fileref);
        err = ferror (uptr->fileref);
        for ( ; i < wc; i++)                            /* fill buffer */
            rlxb[i] = 0;
        if ((t = Map_WriteW (ma, wc << 1, rlxb))) {     /* store buffer */
            rlcs = rlcs | RLCS_ERR | RLCS_NXM;          /* nxm */
            wc = wc - t;                                /* adjust wc */
            }
        }                                               /* end read */

else
if ((uptr->FNC == RLCS_WRITE) && (err == 0)) {          /* write? */
    if ((t = Map_ReadW (ma, wc << 1, rlxb))) {          /* fetch buffer */
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

else
if ((uptr->FNC == RLCS_WCHK) && (err == 0)) {           /* write check? */
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

/* Complete Write Check, Write, Read, Read no header */
rlmp = (rlmp + wc) & 0177777;                           /* final word count */
if (rlmp != 0)                                          /* completed? */
    rlcs |= RLCS_ERR | RLCS_INCMP | RLCS_HDE;

ma += (wc << 1);                                        /* final byte addr */
rlbae = (ma >> 16) & RLBAE_IMP;                         /* upper 6b */
rlba = ma & RLBA_IMP;                                   /* lower 16b */
rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);

/*
If we ran off the end of the track, return 40 in rlda, but keep
track over a legitimate sector (0)?
*/
rlda += ((wc + (RL_NUMWD - 1)) / RL_NUMWD);
/* update head pos */
if (uptr->FNC == RLCS_RNOHDR)
    uptr->TRK = (uptr->TRK & ~RLDA_M_SECT) |
      ((uptr->TRK + ((wc + (RL_NUMWD - 1)) / RL_NUMWD)) & RLDA_M_SECT);
else
    uptr->TRK = rlda;
if (GET_SECT (uptr->TRK) >= RL_NUMSC)
    uptr->TRK &= ~RLDA_M_SECT;                          /* wrap to 0 */

rl_set_done (0);

if (err != 0) {                                         /* error? */
    sim_perror ("RL I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Set done and possibly errors */

void rl_set_done (int32 status)
{
rlcs |= status | CSR_DONE;                              /* set done */
if (rlcs & CSR_IE)
    SET_INT (RL);
else CLR_INT (RL);
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
    uptr->STAT &= ~RLDS_ERR;
    }
if (rlxb == NULL)
    rlxb = (uint16 *) calloc (RL_MAXFR, sizeof (uint16));
if (rlxb == NULL)
    return SCPE_MEM;
return auto_config (0, 0);
}

/* Attach routine */

t_stat rl_attach (UNIT *uptr, CONST char *cptr)
{
uint32 p;
t_stat r;

uptr->capac = (uptr->flags & UNIT_RL02)? RL02_SIZE: RL01_SIZE;
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
/*
For compatibility with existing SIMH behavior, set the drive state
as if the load procedure had already executed.
*/
uptr->TRK = 0;                                          /* cylinder 0 */
uptr->STAT = RLDS_HDO | RLDS_BHO | RLDS_VCK | RLDS_LOCK; /* new volume */
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

t_stat rl_detach (UNIT *uptr)
{
t_stat stat;

sim_cancel (uptr);
stat = detach_unit (uptr);
uptr->STAT = RLDS_BHO | RLDS_LOAD;
return (stat);
}

/* Set size routine */

t_stat rl_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = (val & UNIT_RL02)? RL02_SIZE: RL01_SIZE;
return SCPE_OK;
}

/* Set bad block routine */

t_stat rl_set_bad (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
return pdp11_bad_block (uptr, RL_NUMSC, RL_NUMWD);
}

t_stat rl_set_cover (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    /* allowed only if in LOAD state */
    if ((uptr->STAT & RLDS_M_STATE) != RLDS_LOAD)
        return (SCPE_NOFNC);
    uptr->STAT = (uptr->STAT & ~RLDS_CVO) | val;
    return (SCPE_OK);
}

t_stat rl_show_cover (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (st, "cover %s", (uptr->STAT & RLDS_CVO) ? "open" : "closed");
    return (SCPE_OK);
}

/* simulate the LOAD button on the drive */
t_stat rl_set_load (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (val == 0) {                                     /* LOAD */
        if (uptr->STAT & RLDS_CVO)                      /* cover open? */
            return (SCPE_NOFNC);
        /* spin error if no cartridge loaded */
        if (!(uptr->flags & UNIT_ATT)) {
            uptr->STAT |= RLDS_SPE;
            return (SCPE_NOFNC);
            }
        /* state load? */
        uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_LOAD;
    } else {                                            /* UNLOAD */
        if ((uptr->STAT & RLDS_M_STATE) != RLDS_LOCK)
            return (SCPE_OK);
        uptr->STAT = (uptr->STAT & ~RLDS_M_STATE) | RLDS_UNL;
    }
    uptr->FNC = RLCS_SPECIAL;
    sim_activate (uptr, 10 * rl_swait);
    return (SCPE_OK);
}

t_stat rl_show_load (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (st, "load %s",
        ((uptr->STAT & RLDS_M_STATE) != RLDS_LOAD) ? "set" : "reset");
    return (SCPE_OK);
}

t_stat rl_show_dstate (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32   cnt;

    fprintf (st, "drive state: %s\n", state[(uptr->STAT & RLDS_M_STATE)]);
    fprintf (st, "brushes: %s, heads: %s, cover: %s\n",
        (uptr->STAT & RLDS_BHO) ? "home" : "out",
        (uptr->STAT & RLDS_HDO) ? "out" : "in",
        (uptr->STAT & RLDS_CVO) ? "open" : "closed");
    fprintf (st, "vck:%c, wge:%c, spe:%c\n",
        (uptr->STAT & RLDS_VCK) ? '1' : '0',
        (uptr->STAT & RLDS_WGE) ? '1' : '0',
        (uptr->STAT & RLDS_SPE) ? '1' : '0');
    if (uptr->flags & UNIT_ATT) {
        if ((cnt = sim_activate_time (uptr)) != 0)
            fprintf (st, "FNC: %d, %d\n", uptr->FNC, cnt);
        else
            fputs ("FNC: none\n", st);
        fprintf (st, "TRK: track=%d, cyl=%d, hd=%c, sect=%d\n",
            GET_TRACK (uptr->TRK), GET_CYL (uptr->TRK),
            (uptr->TRK & RLDA_HD1) ? '1' : '0',
            GET_SECT (uptr->TRK));
    }
    return (SCPE_OK);
}

#if defined (VM_PDP11)

/* Handle SET RL RLV12|RLV11 */
t_stat rl_set_ctrl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (UNIBUS)
        return (SCPE_NOFNC);
    if ((val & DEV_RLV11) && (MEMSIZE > UNIMEMSIZE))
        return (SCPE_NOFNC);
    rl_dev.flags = (rl_dev.flags & ~(DEV_RLV11|DEV_Q18)) | val;
    return (SCPE_OK);
}

#endif

/* SHOW RL will display the controller type */
t_stat rl_show_ctrl (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    const char *s = "RLV12";

    if (UNIBUS)
        s = "RL11";
    else if (rl_dev.flags & DEV_RLV11)
        s = "RLV11";
    fputs (s, st);
    return (SCPE_OK);
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
size_t i;
extern uint16 *M;

for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & RLCS_M_DRIVE;
M[BOOT_CSR >> 1] = rl_dib.ba & 0177777;
cpu_set_boot (BOOT_ENTRY);
return SCPE_OK;
}

#else

t_stat rl_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_NOFNC;
}

#endif

t_stat rl_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RL11/RL01/RL02 Cartridge Disk (RL)\n\n");
fprintf (st, "RL11 options include the ability to set units write enabled or write locked,\n");
fprintf (st, "to set the drive type to RL01, RL02, or autosize, and to write a DEC standard\n");
fprintf (st, "044 compliant bad block table on the last track:\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The bad block option can be used only when a unit is attached to a file.\n");
#if defined (VM_PDP11)
fprintf (st, "The RL device supports the BOOT command.\n");
#endif
fprint_reg_help (st, dptr);
fprintf (st, "\nError handling is as follows:\n\n");
fprintf (st, "    error         STOP_IOE   processed as\n");
fprintf (st, "    not attached  1          report error and stop\n");
fprintf (st, "                  0          disk not ready\n\n");
fprintf (st, "    end of file   x          assume rest of disk is zero\n");
fprintf (st, "    OS I/O error  x          report error and stop\n");
return SCPE_OK;
}

const char *rl_description (DEVICE *dptr)
{
return (UNIBUS) ? "RL11/RL01(2) cartridge disk controller" :
                  "RLV12/RL01(2) cartridge disk controller";
}
