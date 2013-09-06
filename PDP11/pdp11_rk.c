/* pdp11_rk.c: RK11/RKV11 cartridge disk simulator

   Copyright (c) 1993-2009, Robert M Supnik

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

   rk           RK11/RKV11/RK05 cartridge disk

   20-Mar-09    RMS     Fixed bug in read header (Walter F Mueller)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   07-Jul-05    RMS     Removed extraneous externs
   30-Sep-04    RMS     Revised Unibus interface
   24-Jan-04    RMS     Added increment inhibit, overrun detection, formatting
   29-Dec-03    RMS     Added RKV11 support
   29-Sep-02    RMS     Added variable address support to bootstrap
                        Added vector change/display support
                        Revised mapping mnemonics
                        New data structures
   26-Jan-02    RMS     Revised bootstrap to conform to M9312
   06-Jan-02    RMS     Revised enable/disable support
   30-Nov-01    RMS     Added read only unit, extended SET/SHOW support
   24-Nov-01    RMS     Converted FLG to array
   09-Nov-01    RMS     Added bus map support
   07-Sep-01    RMS     Revised device disable and interrupt mechanisms
   26-Apr-01    RMS     Added device enable/disable support
   25-Mar-01    RMS     Fixed block fill calculation
   15-Feb-01    RMS     Corrected bootstrap string
   29-Jun-96    RMS     Added unit disable support

   The RK11 is an eight drive cartridge disk subsystem.  An RK05 drive
   consists of 203 cylinders, each with 2 surfaces containing 12 sectors
   of 512 bytes.

   The most complicated part of the RK11 controller is the concept of
   interrupt "polling".  While only one read or write can occur at a
   time, the controller supports multiple seeks.  When a seek completes,
   if done is set the drive attempts to interrupt.  If an interrupt is
   already pending, the interrupt is "queued" until it can be processed.
   When an interrupt occurs, RKDS<15:13> is loaded with the number of the
   interrupting drive.

   To implement this structure, and to assure that read/write interrupts
   take priority over seek interrupts, the controller contains an
   interrupt queue, rkintq, with a bit for a controller interrupt and
   then one for each drive.  In addition, the drive number of the last
   non-seeking drive is recorded in last_drv.
*/

#include "pdp11_defs.h"

/* Constants */

#define RK_NUMWD        256                             /* words/sector */
#define RK_NUMSC        12                              /* sectors/surface */
#define RK_NUMSF        2                               /* surfaces/cylinder */
#define RK_NUMCY        203                             /* cylinders/drive */
#define RK_NUMTR        (RK_NUMCY * RK_NUMSF)           /* tracks/drive */
#define RK_NUMDR        8                               /* drives/controller */
#define RK_M_NUMDR      07
#define RK_SIZE         (RK_NUMCY * RK_NUMSF * RK_NUMSC * RK_NUMWD)
                                                        /* words/drive */
#define RK_CTLI         1                               /* controller int */
#define RK_SCPI(x)      (2u << (x))                     /* drive int */
#define RK_MAXFR        (1 << 16)                       /* max transfer */

/* Flags in the unit flags word */

#define UNIT_V_HWLK     (UNIT_V_UF + 0)                 /* hwre write lock */
#define UNIT_V_SWLK     (UNIT_V_UF + 1)                 /* swre write lock */
#define UNIT_HWLK       (1u << UNIT_V_HWLK)
#define UNIT_SWLK       (1u << UNIT_V_SWLK)
#define UNIT_WPRT       (UNIT_HWLK|UNIT_SWLK|UNIT_RO)   /* write prot */

/* Parameters in the unit descriptor */

#define CYL             u3                              /* current cylinder */
#define FUNC            u4                              /* function */

/* RKDS */

BITFIELD rk_ds_bits[] = {
#define RKDS_SC         0000017                         /* sector counter */
  BITF(SC,4),
#define RKDS_ON_SC      0000020                         /* on sector */
  BIT(ON_SC),
#define RKDS_WLK        0000040                         /* write locked */
  BIT(WLK),
#define RKDS_RWS        0000100                         /* rd/wr/seek ready */
  BIT(RWS),
#define RKDS_RDY        0000200                         /* drive ready */
  BIT(RDY),
#define RKDS_SC_OK      0000400                         /* SC valid */
  BIT(SC_OK),
#define RKDS_INC        0001000                         /* seek incomplete */
  BIT(INC),
#define RKDS_UNSAFE     0002000                         /* unsafe */
  BIT(UNSAFE),
#define RKDS_RK05       0004000                         /* RK05 */
  BIT(RK05),
#define RKDS_PWR        0010000                         /* power low */
  BIT(PWR),
#define RKDS_ID         0160000                         /* drive ID */
#define RKDS_V_ID       13
  BITF(ID,3),
  ENDBITS
};

/* RKER */

BITFIELD rk_er_bits[] = {
#define RKER_WCE        0000001                         /* write check */
  BIT(WCE),
#define RKER_CSE        0000002                         /* checksum */
  BIT(CSE),
#define RKER_NXS        0000040                         /* nx sector */
  BIT(NXS),
#define RKER_NXC        0000100                         /* nx cylinder */
  BIT(NXC),
#define RKER_NXD        0000200                         /* nx drive */
  BIT(NXD),
#define RKER_TE         0000400                         /* timing error */
  BIT(TE),
#define RKER_DLT        0001000                         /* data late */
  BIT(DLT),
#define RKER_NXM        0002000                         /* nx memory */
  BIT(NXM),
#define RKER_PGE        0004000                         /* programming error */
  BIT(PGE),
#define RKER_SKE        0010000                         /* seek error */
  BIT(SKE),
#define RKER_WLK        0020000                         /* write lock */
  BIT(WLK),
#define RKER_OVR        0040000                         /* overrun */
  BIT(OVR),
#define RKER_DRE        0100000                         /* drive error */
  BIT(DRE),
#define RKER_IMP        0177743                         /* implemented */
#define RKER_SOFT       (RKER_WCE+RKER_CSE)             /* soft errors */
#define RKER_HARD       0177740                         /* hard errors */
  ENDBITS
};

/* RKCS */

static const char *rk_funcs[] = {
    "CTLRESET", "WRITE", "READ", "WCHK", "SEEK", "RCHK", "DRVRESET", "WLK"};

BITFIELD rk_cs_bits[] = {
  BIT(GO),
#define RKCS_M_FUNC     0000007                         /* function */
#define  RKCS_CTLRESET  0
#define  RKCS_WRITE     1
#define  RKCS_READ      2
#define  RKCS_WCHK      3
#define  RKCS_SEEK      4
#define  RKCS_RCHK      5
#define  RKCS_DRVRESET  6
#define  RKCS_WLK       7
#define RKCS_V_FUNC     1
  BITFNAM(FUNC,3,rk_funcs),
#define RKCS_MEX        0000060                         /* memory extension */
#define RKCS_V_MEX      4
  BITF(MEX,2),
  BIT(IE),
  BIT(DONE),
#define RKCS_SSE        0000400                         /* stop on soft err */
  BIT(SSE),
  BITNC,
#define RKCS_FMT        0002000                         /* format */
  BIT(FMT),
#define RKCS_INH        0004000                         /* inhibit increment */
  BIT(INH),
  BITNC,
#define RKCS_SCP        0020000                         /* search complete */
  BIT(SCP),
#define RKCS_HERR       0040000                         /* hard error */
  BIT(HERR),
#define RKCS_ERR        0100000                         /* error */
  BIT(ERR),
#define RKCS_REAL       0026776                         /* kept here */
#define RKCS_RW         0006576                         /* read/write */
#define GET_FUNC(x)     (((x) >> RKCS_V_FUNC) & RKCS_M_FUNC)
  ENDBITS
};

/* RKDA */

BITFIELD rk_da_bits[] = {
#define RKDA_V_SECT     0                               /* sector */
#define RKDA_M_SECT     017
  BITF(SECT,4),
#define RKDA_V_TRACK    4                               /* track */
#define RKDA_M_TRACK    0777
  BITF(SURF,1),
#define RKDA_V_CYL      5                               /* cylinder */
#define RKDA_M_CYL      0377
  BITF(CYL,8),
#define RKDA_V_DRIVE    13                              /* drive */
#define RKDA_M_DRIVE    07
#define RKDA_DRIVE      (RKDA_M_DRIVE << RKDA_V_DRIVE)
  BITF(DRIVE,3),
#define GET_SECT(x)     (((x) >> RKDA_V_SECT) & RKDA_M_SECT)
#define GET_CYL(x)      (((x) >> RKDA_V_CYL) & RKDA_M_CYL)
#define GET_TRACK(x)    (((x) >> RKDA_V_TRACK) & RKDA_M_TRACK)
#define GET_DRIVE(x)    (((x) >> RKDA_V_DRIVE) & RKDA_M_DRIVE)
#define GET_DA(x)       ((GET_TRACK (x) * RK_NUMSC) + GET_SECT (x))
  ENDBITS
};

/* RKWC */

BITFIELD rk_wc_bits[] = {
  BITF(WC,16),
  ENDBITS
};

/* RKBA */

BITFIELD rk_ba_bits[] = {
#define RKBA_IMP        0177776                         /* implemented */
  BITF(BA,16),
  ENDBITS
};

BITFIELD *rk_reg_bits[] = {
    rk_ds_bits,
    rk_er_bits,
    rk_cs_bits,
    rk_wc_bits,
    rk_ba_bits,
    rk_da_bits,
    NULL,
    NULL,
    };

/* Debug detail levels */

#define RKDEB_OPS       001                             /* transactions */
#define RKDEB_RRD       002                             /* reg reads */
#define RKDEB_RWR       004                             /* reg writes */
#define RKDEB_TRC       010                             /* trace */
#define RKDEB_INT       020                             /* interrupts */



#define RK_MIN          10
#define MAX(x,y)        (((x) > (y))? (x): (y))

extern uint16 *M;                                       /* memory */
extern int32 int_req[IPL_HLVL];

uint16 *rkxb = NULL;                                    /* xfer buffer */
int32 rkcs = 0;                                         /* control/status */
int32 rkds = 0;                                         /* drive status */
int32 rkba = 0;                                         /* memory address */
int32 rkda = 0;                                         /* disk address */
int32 rker = 0;                                         /* error status */
int32 rkwc = 0;                                         /* word count */
int32 rkintq = 0;                                       /* interrupt queue */
int32 last_drv = 0;                                     /* last r/w drive */
int32 rk_stopioe = 1;                                   /* stop on error */
int32 rk_swait = 10;                                    /* seek time */
int32 rk_rwait = 10;                                    /* rotate time */

char *rk_regnames[] = {
    "RKDS",
    "RKER",
    "RKCS",
    "RKWC",
    "RKBA",
    "RKDA",
    "unused",
    "RKDB",
    };

int32 *rk_regs[] = {
    &rkds,
    &rker,
    &rkcs,
    &rkwc,
    &rkba,
    &rkda,
    };

DEVICE rk_dev;
t_stat rk_rd (int32 *data, int32 PA, int32 access);
t_stat rk_wr (int32 data, int32 PA, int32 access);
int32 rk_inta (void);
t_stat rk_svc (UNIT *uptr);
t_stat rk_reset (DEVICE *dptr);
void rk_go (void);
void rk_set_done (int32 error);
void rk_clr_done (void);
t_stat rk_boot (int32 unitno, DEVICE *dptr);
t_stat rk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *rk_description (DEVICE *dptr);

DEBTAB rk_deb[] = {
    { "OPS", RKDEB_OPS },
    { "RRD", RKDEB_RRD },
    { "RWR", RKDEB_RWR },
    { "INTERRUPT", RKDEB_INT },
    { "TRACE", RKDEB_TRC },
    { NULL, 0 }
    };

/* RK11 data structures

   rk_dev       RK device descriptor
   rk_unit      RK unit list
   rk_reg       RK register list
   rk_mod       RK modifier list
*/

#define IOLN_RK         020

DIB rk_dib = {
    IOBA_AUTO, IOLN_RK, &rk_rd, &rk_wr,
    1, IVCL (RK), VEC_AUTO, { &rk_inta }, IOLN_RK,
    };

UNIT rk_unit[] = {
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) },
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) },
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) },
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) },
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) },
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) },
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) },
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) }
    };

REG rk_reg[] = {
    { ORDATADF (RKCS, rkcs, 16, "control/status", rk_cs_bits) },
    { ORDATADF (RKDA, rkda, 16, "disk address", rk_da_bits) },
    { ORDATADF (RKBA, rkba, 16, "memory address", rk_ba_bits) },
    { ORDATADF (RKWC, rkwc, 16, "word count", rk_wc_bits) },
    { ORDATADF (RKDS, rkds, 16, "drive status", rk_ds_bits) },
    { ORDATADF (RKER, rker, 16, "error status", rk_er_bits) },
    { ORDATAD (INTQ, rkintq, 9, "interrupt queue") },
    { ORDATAD (DRVN, last_drv, 3, "last r/w drive") },
    { FLDATAD (INT, IREQ (RK), INT_V_RK, "interrupt pending flag") },
    { FLDATAD (ERR, rkcs, CSR_V_ERR, "error flag (CSR<15>)") },
    { FLDATAD (DONE, rkcs, CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (IE, rkcs, CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (STIME, rk_swait, 24, "seek time, per cylinder"), PV_LEFT },
    { DRDATAD (RTIME, rk_rwait, 24, "rotational delay"), PV_LEFT },
    { FLDATAD (STOP_IOE, rk_stopioe, 0, "stop on I/O error flag") },
    { ORDATA (DEVADDR, rk_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, rk_dib.vec, 16), REG_HRO },
    { NULL }
    };

MTAB rk_mod[] = {
    { UNIT_HWLK,        0, "write enabled", "WRITEENABLED", 
        NULL, NULL, NULL, "Write enable disk drive" },
    { UNIT_HWLK, UNIT_HWLK, "write locked",  "LOCKED", 
        NULL, NULL, NULL, "Write lock disk drive"  },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 010, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 }
    };

DEVICE rk_dev = {
    "RK", rk_unit, rk_reg, rk_mod,
    RK_NUMDR, 8, 24, 1, 8, 16,
    NULL, NULL, &rk_reset,
    &rk_boot, NULL, NULL,
    &rk_dib, DEV_DISABLE | DEV_UBUS | DEV_Q18 | DEV_DEBUG, 0,
    rk_deb, NULL, NULL, &rk_help, NULL, NULL,
    &rk_description 
    };

/* I/O dispatch routine, I/O addresses 17777400 - 17777416

   17777400     RKDS    read only, constructed from "id'd drive"
                        plus current drive status flags
   17777402     RKER    read only, set as operations progress,
                        cleared by INIT or CONTROL RESET
   17777404     RKCS    read/write
   17777406     RKWC    read/write
   17777410     RKBA    read/write
   17777412     RKDA    read/write
   17777414     RKMR    read/write, unimplemented
   17777416     RKDB    read only, unimplemented
*/

t_stat rk_rd (int32 *data, int32 PA, int32 access)
{
UNIT *uptr;

switch ((PA >> 1) & 07) {                               /* decode PA<3:1> */

    case 0:                                             /* RKDS: read only */
        rkds = (rkds & RKDS_ID) | RKDS_SC_OK |
            (rand () % RK_NUMSC);                       /* random sector */
        uptr = rk_dev.units + GET_DRIVE (rkda);         /* selected unit */
        if (!(uptr->flags & UNIT_DIS))                  /* enabled? */
            rkds = rkds | RKDS_RK05;
        if (uptr->flags & UNIT_ATT)                     /* attached? */
            rkds = rkds | RKDS_RDY;
        if (!sim_is_active (uptr))                      /* idle? */
            rkds = rkds | RKDS_RWS;
        if (uptr->flags & UNIT_WPRT)
            rkds = rkds | RKDS_WLK;
        if (GET_SECT (rkda) == (rkds & RKDS_SC))
            rkds = rkds | RKDS_ON_SC;
        *data = rkds;
        break;

    case 1:                                             /* RKER: read only */
        *data = rker & RKER_IMP;
        break;

    case 2:                                             /* RKCS */
        rkcs = rkcs & RKCS_REAL;
        if (rker)                                       /* update err flags */
            rkcs = rkcs | RKCS_ERR;
        if (rker & RKER_HARD)
            rkcs = rkcs | RKCS_HERR;
        *data = rkcs;
        break;

    case 3:                                             /* RKWC */
        *data = rkwc;
        break;

    case 4:                                             /* RKBA */
        *data = rkba & RKBA_IMP;
        break;

    case 5:                                             /* RKDA */
        *data = rkda;
        break;

    default:
        *data = 0;
        return SCPE_OK;
        }                                               /* end switch */
sim_debug (RKDEB_RRD, &rk_dev, ">>RK read: %s=0%o\n", rk_regnames[(PA >> 1) & 07], *data);
sim_debug_bits (RKDEB_RRD, &rk_dev, rk_reg_bits[(PA >> 1) & 07], *data, *data, 1);
return SCPE_OK;
}

t_stat rk_wr (int32 data, int32 PA, int32 access)
{
int32 old_val = *rk_regs[(PA >> 1) & 07], new_val = 0;

switch ((PA >> 1) & 07) {                               /* decode PA<3:1> */

    case 0:                                             /* RKDS: read only */
        break;

    case 1:                                             /* RKER: read only */
        break;

    case 2:                                             /* RKCS */
        rkcs = rkcs & RKCS_REAL;
        if (access == WRITEB)
            data = (PA & 1)? (rkcs & 0377) | (data << 8): (rkcs & ~0377) | data;
        if ((data & CSR_IE) == 0) {                     /* int disable? */
            rkintq = 0;                                 /* clr int queue */
            sim_debug (RKDEB_INT, &rk_dev, "rk_wr(CLR_INT)\n");
            CLR_INT (RK);                               /* clr int request */
            }
        else if ((rkcs & (CSR_DONE + CSR_IE)) == CSR_DONE) {
            rkintq = rkintq | RK_CTLI;                  /* queue ctrl int */
            sim_debug (RKDEB_INT, &rk_dev, "rk_wr(SET_INT)\n");
            SET_INT (RK);                               /* set int request */
            }
        rkcs = (rkcs & ~RKCS_RW) | (data & RKCS_RW);
        if ((rkcs & CSR_DONE) && (data & CSR_GO))       /* new function? */
            rk_go ();
        break;

    case 3:                                             /* RKWC */
        if (access == WRITEB)
            data = (PA & 1)? (rkwc & 0377) | (data << 8): (rkwc & ~0377) | data;
        rkwc = data;
        break;

    case 4:                                             /* RKBA */
        if (access == WRITEB)
            data = (PA & 1)? (rkba & 0377) | (data << 8): (rkba & ~0377) | data;
        rkba = data & RKBA_IMP;
        break;

    case 5:                                             /* RKDA */
        if ((rkcs & CSR_DONE) == 0)
            return SCPE_OK;
        if (access == WRITEB)
            data = (PA & 1)? (rkda & 0377) | (data << 8): (rkda & ~0377) | data;
        rkda = data;
        break;

    default:
        return SCPE_OK;
        }                                               /* end switch */
sim_debug (RKDEB_RWR, &rk_dev, ">>RK write: %s=0%o\n", rk_regnames[(PA >> 1) & 07], data);
sim_debug_bits (RKDEB_RWR, &rk_dev, rk_reg_bits[(PA >> 1) & 07], old_val, *rk_regs[(PA >> 1) & 07], 1);
return SCPE_OK;
}

/* Initiate new function */

void rk_go (void)
{
int32 i, sect, cyl, func;
UNIT *uptr;

func = GET_FUNC (rkcs);                                 /* get function */
if (func == RKCS_CTLRESET) {                            /* control reset? */
    rker = 0;                                           /* clear errors */
    rkda = 0;
    rkba = 0;
    rkcs = CSR_DONE;
    rkintq = 0;                                         /* clr int queue */
    sim_debug (RKDEB_INT, &rk_dev, "rk_go(CLR_INT)\n");
    CLR_INT (RK);                                       /* clr int request */
    return;
    }
rker = rker & ~RKER_SOFT;                               /* clear soft errors */
if (rker == 0)                                          /* redo summary */
    rkcs = rkcs & ~RKCS_ERR;
rkcs = rkcs & ~RKCS_SCP;                                /* clear sch compl*/
rk_clr_done ();                                         /* clear done */
last_drv = GET_DRIVE (rkda);                            /* get drive no */
uptr = rk_dev.units + last_drv;                         /* select unit */
if (uptr->flags & UNIT_DIS) {                           /* not present? */
    rk_set_done (RKER_NXD);
    return;
    }
if (((uptr->flags & UNIT_ATT) == 0) ||                  /* not att or busy? */
    sim_is_active (uptr)) {
    rk_set_done (RKER_DRE);
    return;
    }
if ((rkcs & RKCS_FMT) &&                                /* format and */
    (func != RKCS_READ) && (func != RKCS_WRITE)) {      /* not read or write? */
    rk_set_done (RKER_PGE);
    return;
    }
if ((func == RKCS_WRITE) &&                             /* write and locked? */
    (uptr->flags & UNIT_WPRT)) {
    rk_set_done (RKER_WLK);
    return;
    }
if (func == RKCS_WLK) {                                 /* write lock? */
    uptr->flags = uptr->flags | UNIT_SWLK;
    rk_set_done (0);
    return;
    }
if (func == RKCS_DRVRESET) {                            /* drive reset? */
    uptr->flags = uptr->flags & ~UNIT_SWLK;
    cyl = sect = 0;
    func = RKCS_SEEK;
    }
else {
    sect = GET_SECT (rkda);
    cyl = GET_CYL (rkda);
    }
if (sect >= RK_NUMSC) {                                 /* bad sector? */
    rk_set_done (RKER_NXS);
    return;
    }
if (cyl >= RK_NUMCY) {                                  /* bad cyl? */
    rk_set_done (RKER_NXC);
    return;
    }
i = abs (cyl - uptr->CYL) * rk_swait;                   /* seek time */
if (func == RKCS_SEEK) {                                /* seek? */
    rk_set_done (0);                                    /* set done */
    sim_activate (uptr, MAX (RK_MIN, i));               /* schedule */
    }
else sim_activate (uptr, i + rk_rwait);
uptr->FUNC = func;                                      /* save func */
uptr->CYL = cyl;                                        /* put on cylinder */
return;
}

/* Service unit timeout

   If seek in progress, complete seek command
   Else complete data transfer command

   The unit control block contains the function and disk address for
   the current command.
*/

t_stat rk_svc (UNIT *uptr)
{
int32 i, drv, err, awc, wc, cma, cda, t;
int32 da, cyl, track, sect;
uint32 ma;
uint16 comp;

drv = (int32) (uptr - rk_dev.units);                    /* get drv number */
if (uptr->FUNC == RKCS_SEEK) {                          /* seek */
    rkcs = rkcs | RKCS_SCP;                             /* set seek done */
    if (rkcs & CSR_IE) {                                /* ints enabled? */
        rkintq = rkintq | RK_SCPI (drv);                /* queue request */
        if (rkcs & CSR_DONE) {
            sim_debug (RKDEB_INT, &rk_dev, "rk_svc(SET_INT)\n");
            SET_INT (RK);
            }
        }
    else {
        rkintq = 0;                                     /* clear queue */
        sim_debug (RKDEB_INT, &rk_dev, "rk_svc(CLR_INT)\n");
        CLR_INT (RK);                                   /* clear interrupt */
        }
    return SCPE_OK;
    }

if ((uptr->flags & UNIT_ATT) == 0) {                    /* attached? */
    rk_set_done (RKER_DRE);
    return IORETURN (rk_stopioe, SCPE_UNATT);
    }
sect = GET_SECT (rkda);                                 /* get sector, cyl */
cyl = GET_CYL (rkda);
if (sect >= RK_NUMSC) {                                 /* bad sector? */
    rk_set_done (RKER_NXS);
    return SCPE_OK;
    }
if (cyl >= RK_NUMCY) {                                  /* bad cyl? */
    rk_set_done (RKER_NXC);
    return SCPE_OK;
    }
ma = ((rkcs & RKCS_MEX) << (16 - RKCS_V_MEX)) | rkba;   /* get mem addr */
da = GET_DA (rkda) * RK_NUMWD;                          /* get disk addr */
wc = 0200000 - rkwc;                                    /* get wd cnt */
if ((da + wc) > (int32) uptr->capac) {                  /* overrun? */
    wc = uptr->capac - da;                              /* trim transfer */
    rker = rker | RKER_OVR;                             /* set overrun err */
    }

err = fseek (uptr->fileref, da * sizeof (int16), SEEK_SET);
if (wc && (err == 0)) {                                 /* seek ok? */
    switch (uptr->FUNC) {                               /* case on function */

    case RKCS_READ:                                     /* read */
        if (rkcs & RKCS_FMT) {                          /* format? */
            for (i = 0, cda = da; i < wc; i++) {        /* fill buffer with cyl #s */
                if (cda >= (int32) uptr->capac) {       /* overrun? */
                    rker = rker | RKER_OVR;             /* set overrun err */
                    wc = i;                             /* trim transfer */
                    break;
                    }
                rkxb[i] = ((cda / RK_NUMWD) / (RK_NUMSF * RK_NUMSC)) << RKDA_V_CYL;
                cda = cda + RK_NUMWD;                   /* next sector */
                }                                       /* end for wc */
            }                                           /* end if format */
        else {                                          /* normal read */
            i = fxread (rkxb, sizeof (int16), wc, uptr->fileref);
            err = ferror (uptr->fileref);               /* read file */
            for ( ; i < wc; i++)                        /* fill buf */
                rkxb[i] = 0;
            }
        if (rkcs & RKCS_INH) {                          /* incr inhibit? */
            if ((t = Map_WriteW (ma, 2, &rkxb[wc - 1]))) {/* store last */
                rker = rker | RKER_NXM;                 /* NXM? set flag */
                wc = 0;                                 /* no transfer */
                }
            }
        else {                                          /* normal store */
            if ((t = Map_WriteW (ma, wc << 1, rkxb))) { /* store buf */
                rker = rker | RKER_NXM;                 /* NXM? set flag */
                wc = wc - t;                            /* adj wd cnt */
                }
            }
        break;                                          /* end read */

    case RKCS_WRITE:                                    /* write */
        if (rkcs & RKCS_INH) {                          /* incr inhibit? */
            if ((t = Map_ReadW (ma, 2, &comp))) {       /* get 1st word */
                rker = rker | RKER_NXM;                 /* NXM? set flag */
                wc = 0;                                 /* no transfer */
                }
            for (i = 0; i < wc; i++)                    /* all words same */
                rkxb[i] = comp;
            }
        else {                                          /* normal fetch */
            if ((t = Map_ReadW (ma, wc << 1, rkxb))) {  /* get buf */
                rker = rker | RKER_NXM;                 /* NXM? set flg */
                wc = wc - t;                            /* adj wd cnt */
                }
            }
        if (wc) {                                       /* any xfer? */
            awc = (wc + (RK_NUMWD - 1)) & ~(RK_NUMWD - 1); /* clr to */
            for (i = wc; i < awc; i++)                  /* end of blk */
                rkxb[i] = 0;
            fxwrite (rkxb, sizeof (int16), awc, uptr->fileref);
            err = ferror (uptr->fileref);
            }
        break;                                          /* end write */

    case RKCS_WCHK:                                     /* write check */
        i = fxread (rkxb, sizeof (int16), wc, uptr->fileref);
        if ((err = ferror (uptr->fileref))) {           /* read error? */
            wc = 0;                                     /* no transfer */
            break;
            }
        for ( ; i < wc; i++)                            /* fill buf */
            rkxb[i] = 0;
        awc = wc;                                       /* save wc */
        for (wc = 0, cma = ma; wc < awc; wc++)  {       /* loop thru buf */
            if (Map_ReadW (cma, 2, &comp)) {            /* mem wd */
                rker = rker | RKER_NXM;                 /* NXM? set flg */
                break;
                }
            if (comp != rkxb[wc])  {                    /* match to disk? */
                rker = rker | RKER_WCE;                 /* no, err */
                if (rkcs & RKCS_SSE)
                    break;
                }
            if (!(rkcs & RKCS_INH))                     /* next mem addr */
                cma = cma + 2;
            }                                           /* end for */
        break;                                          /* end wcheck */

    default:                                            /* read check */
        break;
        }                                               /* end switch */
    }                                                   /* end else */

rkwc = (rkwc + wc) & 0177777;                           /* final word count */
if (!(rkcs & RKCS_INH))                                 /* final byte addr */
    ma = ma + (wc << 1);
rkba = ma & RKBA_IMP;                                   /* lower 16b */
rkcs = (rkcs & ~RKCS_MEX) | ((ma >> (16 - RKCS_V_MEX)) & RKCS_MEX);
if ((uptr->FUNC == RKCS_READ) && (rkcs & RKCS_FMT))     /* read format? */
    da = da + (wc * RK_NUMWD);                          /* count by sectors */
else da = da + wc + (RK_NUMWD - 1);                     /* count by words */
track = (da / RK_NUMWD) / RK_NUMSC;
sect = (da / RK_NUMWD) % RK_NUMSC;
rkda = (rkda & RKDA_DRIVE) | (track << RKDA_V_TRACK) | (sect << RKDA_V_SECT);
rk_set_done (0);

if (err != 0) {                                         /* error? */
    perror ("RK I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Interrupt state change routines

   rk_set_done          set done and possibly errors
   rk_clr_done          clear done
   rk_inta              acknowledge intererupt
*/

void rk_set_done (int32 error)
{
rkcs = rkcs | CSR_DONE;                                 /* set done */
if (error != 0) {
    rker = rker | error;                                /* update error */
    if (rker)                                           /* update err flags */
        rkcs = rkcs | RKCS_ERR;
    if (rker & RKER_HARD)
        rkcs = rkcs | RKCS_HERR;
    }
if (rkcs & CSR_IE) {                                    /* int enable? */
    rkintq = rkintq | RK_CTLI;                          /* set ctrl int */
    sim_debug (RKDEB_INT, &rk_dev, "rk_set_done(SET_INT)\n");
    SET_INT (RK);                                       /* request int */
    }
else {
    rkintq = 0;                                         /* clear queue */
    sim_debug (RKDEB_INT, &rk_dev, "rk_set_done(CLR_INT)\n");
    CLR_INT (RK);
    }
return;
}

void rk_clr_done (void)
{
rkcs = rkcs & ~CSR_DONE;                                /* clear done */
rkintq = rkintq & ~RK_CTLI;                             /* clear ctl int */
sim_debug (RKDEB_INT, &rk_dev, "rk_clr_done(CLR_INT)\n");
CLR_INT (RK);                                           /* clear int req */
return;
}

int32 rk_inta (void)
{
int32 i;

for (i = 0; i <= RK_NUMDR; i++) {                       /* loop thru intq */
    if (rkintq & (1u << i)) {                           /* bit i set? */
        rkintq = rkintq & ~(1u << i);                   /* clear bit i */
        if (rkintq) {                                    /* queue next */
            sim_debug (RKDEB_INT, &rk_dev, "rk_inta(SET_INT)\n");
            SET_INT (RK);
            }
        rkds = (rkds & ~RKDS_ID) |                      /* id drive */
            (((i == 0)? last_drv: i - 1) << RKDS_V_ID);
        sim_debug (RKDEB_INT, &rk_dev, "rk_inta(vec=0%o)\n", rk_dib.vec);
        return rk_dib.vec;                              /* return vector */
        }
    }
rkintq = 0;                                           /* clear queue */
return 0;                                               /* passive release */
}

/* Device reset */

t_stat rk_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

rkcs = CSR_DONE;
rkda = rkba = rker = rkds = 0;
rkintq = last_drv = 0;
sim_debug (RKDEB_INT, &rk_dev, "rk_reset(CLR_INT)\n");
CLR_INT (RK);
for (i = 0; i < RK_NUMDR; i++) {
    uptr = rk_dev.units + i;
    sim_cancel (uptr);
    uptr->CYL = uptr->FUNC = 0;
    uptr->flags = uptr->flags & ~UNIT_SWLK;
    }
if (rkxb == NULL)
    rkxb = (uint16 *) calloc (RK_MAXFR, sizeof (uint16));
if (rkxb == NULL)
    return SCPE_MEM;
return auto_config (0, 0);
}

/* Device bootstrap */

#define BOOT_START      02000                           /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 032)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    0042113,                        /* "KD" */
    0012706, BOOT_START,            /* MOV #boot_start, SP */
    0012700, 0000000,               /* MOV #unit, R0        ; unit number */
    0010003,                        /* MOV R0, R3 */
    0000303,                        /* SWAB R3 */
    0006303,                        /* ASL R3 */
    0006303,                        /* ASL R3 */
    0006303,                        /* ASL R3 */
    0006303,                        /* ASL R3 */
    0006303,                        /* ASL R3 */
    0012701, 0177412,               /* MOV #RKDA, R1        ; csr */
    0010311,                        /* MOV R3, (R1)         ; load da */
    0005041,                        /* CLR -(R1)            ; clear ba */
    0012741, 0177000,               /* MOV #-256.*2, -(R1)  ; load wc */
    0012741, 0000005,               /* MOV #READ+GO, -(R1)  ; read & go */
    0005002,                        /* CLR R2 */
    0005003,                        /* CLR R3 */
    0012704, BOOT_START+020,        /* MOV #START+20, R4 */
    0005005,                        /* CLR R5 */
    0105711,                        /* TSTB (R1) */
    0100376,                        /* BPL .-2 */
    0105011,                        /* CLRB (R1) */
    0005007                         /* CLR PC */
    };

t_stat rk_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern int32 saved_PC;

for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & RK_M_NUMDR;
M[BOOT_CSR >> 1] = (rk_dib.ba & DMASK) + 012;
saved_PC = BOOT_ENTRY;
return SCPE_OK;
}

t_stat rk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "RK11/RKV11 cartridge disk (RK05) controller (RK)\n\n");
fprintf (st, "Options include the ability to set units write enabled or write locked,\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe RK11 supports the BOOT command.  The RK11 is disabled in a Qbus\n");
fprintf (st, "system with more than 256KB of memory.\n");
fprint_reg_help (st, dptr);
fprintf (st, "\nError handling is as follows:\n\n");
fprintf (st, "    error         STOP_IOE   processed as\n");
fprintf (st, "    not attached  1          report error and stop\n");
fprintf (st, "                  0          disk not ready\n\n");
fprintf (st, "    end of file   x          assume rest of disk is zero\n");
fprintf (st, "    OS I/O error  x          report error and stop\n");
return SCPE_OK;
}

char *rk_description (DEVICE *dptr)
{
return "RK11/RKV11 cartridge disk controller";
}
