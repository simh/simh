/* pdp10_rp.c - RH11/RP04/05/06/07 RM02/03/05/80 "Massbus" disk controller

   Copyright (c) 1993-2017, Robert M Supnik

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

   rp           RH/RP/RM moving head disks

   13-Mar-17    RMS     Annotated fall through in switch
   12-Nov-05    RMS     Fixed DCLR not to clear drive address
   07-Jul-05    RMS     Removed extraneous externs
   18-Mar-05    RMS     Added attached test to detach routine
   20-Sep-04    RMS     Fixed bugs in replicated state, RP vs RM accuracy
   04-Jan-04    RMS     Changed sim_fsize calling sequence
   23-Jul-03    RMS     Fixed bug in read header stub
   25-Apr-03    RMS     Revised for extended file support
   21-Nov-02    RMS     Fixed bug in bootstrap (reported by Michael Thompson)
   29-Sep-02    RMS     Added variable vector support
                        New data structures
   30-Nov-01    RMS     Added read only unit, extended SET/SHOW support support
   24-Nov-01    RMS     Changed RPER, RPDS, FNC, FLG to arrays
   23-Oct-01    RMS     Fixed bug in error interrupts
                        New IO page address constants
   05-Oct-01    RMS     Rewrote interrupt handling from schematics
   02-Oct-01    RMS     Revised CS1 write code
   30-Sep-01    RMS     Moved CS1<5:0> into drives
   28-Sep-01    RMS     Fixed interrupt handling for SC/ATA
   23-Aug-01    RMS     Added read/write header stubs for ITS
                        (found by Mirian Crzig Lennox) 
   13-Jul-01    RMS     Changed fread call to fxread (Peter Schorn)
   14-May-01    RMS     Added check for unattached drive

   The "Massbus style" disks consisted of several different large
   capacity drives interfaced through a reasonably common (but not
   100% compatible) family of interfaces into the KS10 Unibus via
   the RH11 disk controller.

   WARNING: The interupt logic of the RH11/RH70 is unusual and must be
   simulated with great precision.  The RH11 has an internal interrupt
   request flop, CSTB INTR, which is controlled as follows:
   - Writing IE and DONE simultaneously sets CSTB INTR
   - Controller clear, INIT, and interrupt acknowledge clear CSTB INTR
     (and also clear IE)
   - A transition of DONE from 0 to 1 sets CSTB from INTR
   The output of INTR is OR'd with the AND of RPCS1<SC,DONE,IE> to
   create the interrupt request signal.  Thus,
   - The DONE interrupt is edge sensitive, but the SC interrupt is
     level sensitive.
   - The DONE interrupt, once set, is not disabled if IE is cleared,
     but the SC interrupt is.
*/

#include "pdp10_defs.h"
#include "sim_disk.h"
#include <math.h>

#define RP_NUMDR        8                               /* #drives */
#define RP_NUMWD        128                             /* 36b words/sector */
#define RP_MAXFR        32768                           /* max transfer */
#define SPINUP_DLY      (1000*1000)                     /* Spinup delay, usec */
#define GET_SECTOR(x,d) ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) drv_tab[d].sect)))
#define MBA_RP_CTRL     0                               /* RP drive */
#define MBA_RM_CTRL     1                               /* RM drive */

/* Flags in the unit flags word */

#define UNIT_V_WLK      DKUF_V_WLK                      /* write locked */
#define UNIT_V_DTYPE    (DKUF_V_UF + 0)                 /* disk type */
#define UNIT_W_DTYPE    3                               /* 3b disk type */
#define UNIT_M_DTYPE    7
#define UNIT_V_AUTO     (UNIT_V_DTYPE + UNIT_W_DTYPE)   /* autosize */
#define UNIT_V_UTS      (UNIT_V_AUTO + 1)               /* Up to speed */
#define UNIT_UTS        (1u << UNIT_V_UTS)
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

/* Parameters in the unit descriptor */

#define CYL             u3                              /* current cylinder */
#define FUNC            u4                              /* function */

/* RPCS1 - 176700 - control/status 1 */

#define CS1_GO          CSR_GO                          /* go */
#define CS1_V_FNC       1                               /* function pos */
#define CS1_M_FNC       037                             /* function mask */
#define CS1_FNC         (CS1_M_FNC << CS1_V_FNC)
#define  FNC_NOP        000                             /* no operation */
#define  FNC_UNLOAD     001                             /* unload */
#define  FNC_SEEK       002                             /* seek */
#define  FNC_RECAL      003                             /* recalibrate */
#define  FNC_DCLR       004                             /* drive clear */
#define  FNC_RELEASE    005                             /* port release */
#define  FNC_OFFSET     006                             /* offset */
#define  FNC_RETURN     007                             /* return to center */
#define  FNC_PRESET     010                             /* read-in preset */
#define  FNC_PACK       011                             /* pack acknowledge */
#define  FNC_SEARCH     014                             /* search */
#define FNC_XFER        024                             /* >=? data xfr */
#define  FNC_WCHK       024                             /* write check */
#define  FNC_WRITE      030                             /* write */
#define  FNC_WRITEH     031                             /* write w/ headers */
#define  FNC_READ       034                             /* read */
#define  FNC_READH      035                             /* read w/ headers */
#define CS1_IE          CSR_IE                          /* int enable */
#define CS1_DONE        CSR_DONE                        /* ready */
#define CS1_V_UAE       8                               /* Unibus addr ext */
#define CS1_M_UAE       03
#define CS1_UAE         (CS1_M_UAE << CS1_V_UAE)
#define CS1_DVA         0004000                         /* drive avail NI */
#define CS1_MCPE        0020000                         /* Mbus par err NI */
#define CS1_TRE         0040000                         /* transfer err */
#define CS1_SC          0100000                         /* special cond */
#define CS1_MBZ         0012000
#define CS1_DRV         (CS1_FNC | CS1_GO)
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)
#define GET_UAE(x)      (((x) & CS1_UAE) << (16 - CS1_V_UAE))

/* RPWC - 176702 - word count */

/* RPBA - 176704 - base address */

#define BA_MBZ          0000001                         /* must be zero */

/* RPDA - 176706 - sector/track */

#define DA_V_SC         0                               /* sector pos */
#define DA_M_SC         077                             /* sector mask */
#define DA_V_SF         8                               /* track pos */
#define DA_M_SF         077                             /* track mask */
#define DA_MBZ          0140300
#define GET_SC(x)       (((x) >> DA_V_SC) & DA_M_SC)
#define GET_SF(x)       (((x) >> DA_V_SF) & DA_M_SF)

/* RPCS2 - 176710 - control/status 2 */

#define CS2_V_UNIT      0                               /* unit pos */
#define CS2_M_UNIT      07                              /* unit mask */
#define CS2_UNIT        (CS2_M_UNIT << CS2_V_UNIT)
#define CS2_UAI         0000010                         /* addr inhibit */
#define CS2_PAT         0000020                         /* parity test NI */
#define CS2_CLR         0000040                         /* controller clear */
#define CS2_IR          0000100                         /* input ready */
#define CS2_OR          0000200                         /* output ready */
#define CS2_MDPE        0000400                         /* Mbus par err NI */
#define CS2_MXF         0001000                         /* missed xfer NI */
#define CS2_PGE         0002000                         /* program err */
#define CS2_NEM         0004000                         /* nx mem err */
#define CS2_NED         0010000                         /* nx drive err */
#define CS2_PE          0020000                         /* parity err NI */
#define CS2_WCE         0040000                         /* write check err */
#define CS2_DLT         0100000                         /* data late NI */
#define CS2_MBZ         (CS2_CLR)
#define CS2_RW          (CS2_UNIT | CS2_UAI | CS2_PAT | CS2_MXF | CS2_PE)
#define CS2_ERR         (CS2_MDPE | CS2_MXF | CS2_PGE | CS2_NEM | \
                         CS2_NED | CS2_PE | CS2_WCE | CS2_DLT )
#define GET_UNIT(x)     (((x) >> CS2_V_UNIT) & CS2_M_UNIT)

/* RPDS - 176712 - drive status */

#define DS_OF           0000001                         /* offset mode */
#define DS_VV           0000100                         /* volume valid */
#define DS_RDY          0000200                         /* drive ready */
#define DS_DPR          0000400                         /* drive present */
#define DS_PGM          0001000                         /* programable NI */
#define DS_LST          0002000                         /* last sector */
#define DS_WRL          0004000                         /* write locked */
#define DS_MOL          0010000                         /* medium online */
#define DS_PIP          0020000                         /* pos in progress */
#define DS_ERR          0040000                         /* error */
#define DS_ATA          0100000                         /* attention active */
#define DS_MBZ          0000076

/* RPER1 - 176714 - error status 1 */

#define ER1_ILF         0000001                         /* illegal func */
#define ER1_ILR         0000002                         /* illegal register */
#define ER1_RMR         0000004                         /* reg mod refused */
#define ER1_PAR         0000010                         /* parity err */
#define ER1_FER         0000020                         /* format err NI */
#define ER1_WCF         0000040                         /* write clk fail NI */
#define ER1_ECH         0000100                         /* ECC hard err NI */
#define ER1_HCE         0000200                         /* hdr comp err NI */
#define ER1_HCR         0000400                         /* hdr CRC err NI */
#define ER1_AOE         0001000                         /* addr ovflo err */
#define ER1_IAE         0002000                         /* invalid addr err */
#define ER1_WLE         0004000                         /* write lock err */
#define ER1_DTE         0010000                         /* drive time err NI */
#define ER1_OPI         0020000                         /* op incomplete */
#define ER1_UNS         0040000                         /* drive unsafe */
#define ER1_DCK         0100000                         /* data check NI */

/* RPAS - 176716 - attention summary */

#define AS_U0           0000001                         /* unit 0 flag */

/* RPLA - 176720 - look ahead register */

#define LA_V_SC         6                               /* sector pos */

/* RPDB - 176722 - data buffer */
/* RPMR - 176724 - maintenace register */
/* RPDT - 176726 - drive type */
/* RPSN - 176730 - serial number */

/* RPOF - 176732 - offset register */

#define OF_HCI          0002000                         /* hdr cmp inh NI */
#define OF_ECI          0004000                         /* ECC inhibit NI */
#define OF_F22          0010000                         /* format NI */
#define OF_MBZ          0161400

/* RPDC - 176734 - desired cylinder */

#define DC_V_CY         0                               /* cylinder pos */
#define DC_M_CY         01777                           /* cylinder mask */
#define DC_MBZ          0176000
#define GET_CY(x)       (((x) >> DC_V_CY) & DC_M_CY)
#define GET_DA(c,fs,d)  ((((GET_CY (c) * drv_tab[d].surf) + \
                        GET_SF (fs)) * drv_tab[d].sect) + GET_SC (fs))

/* RPCC -  176736 - current cylinder */
/* RPER2 - 176740 - error status 2 - drive unsafe conditions */
/* RPER3 - 176742 - error status 3 - more unsafe conditions */
/* RPEC1 - 176744 - ECC status 1 - unimplemented */
/* RPEC2 - 176746 - ECC status 2 - unimplemented */

/* This controller supports many different disk drive types.  These drives
   are operated in 576 bytes/sector (128 36b words/sector) mode, which gives
   them somewhat different geometry from the PDP-11 variants:

   type         #sectors/       #surfaces/      #cylinders/
                 surface         cylinder        drive

   RM02/3       30              5               823             =67MB
   RP04/5       20              19              411             =88MB
   RM80         30              14              559             =124MB
   RP06         20              19              815             =176MB
   RM05         30              19              823             =256MB
   RP07         43              32              630             =516MB

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE.

   The RP07, despite its name, uses an RM-style controller.
*/

#define RM03_DTYPE      0
#define RM03_SECT       30
#define RM03_SURF       5
#define RM03_CYL        823
#define RM03_DEV        020024
#define RM03_SIZE       (RM03_SECT * RM03_SURF * RM03_CYL * RP_NUMWD)

#define RP04_DTYPE      1
#define RP04_SECT       20
#define RP04_SURF       19
#define RP04_CYL        411
#define RP04_DEV        020020
#define RP04_SIZE       (RP04_SECT * RP04_SURF * RP04_CYL * RP_NUMWD)

#define RM80_DTYPE      2
#define RM80_SECT       30
#define RM80_SURF       14
#define RM80_CYL        559
#define RM80_DEV        020026
#define RM80_SIZE       (RM80_SECT * RM80_SURF * RM80_CYL * RP_NUMWD)

#define RP06_DTYPE      3
#define RP06_SECT       20
#define RP06_SURF       19
#define RP06_CYL        815
#define RP06_DEV        020022
#define RP06_SIZE       (RP06_SECT * RP06_SURF * RP06_CYL * RP_NUMWD)

#define RM05_DTYPE      4
#define RM05_SECT       30
#define RM05_SURF       19
#define RM05_CYL        823
#define RM05_DEV        020027
#define RM05_SIZE       (RM05_SECT * RM05_SURF * RM05_CYL * RP_NUMWD)

#define RP07_DTYPE      5
#define RP07_SECT       43
#define RP07_SURF       32
#define RP07_CYL        630
#define RP07_DEV        020042
#define RP07_SIZE       (RP07_SECT * RP07_SURF * RP07_CYL * RP_NUMWD)

struct drvtyp {
    int32       sect;                                   /* sectors */
    int32       surf;                                   /* surfaces */
    int32       cyl;                                    /* cylinders */
    int32       size;                                   /* #blocks */
    int32       devtype;                                /* device type */
    int32       ctrl;                                   /* ctrl type */
    const char  *name;                                  /* device type name */
    };

struct drvtyp drv_tab[] = {
    { RM03_SECT, RM03_SURF, RM03_CYL, RM03_SIZE, RM03_DEV, MBA_RM_CTRL, "RM03" },
    { RP04_SECT, RP04_SURF, RP04_CYL, RP04_SIZE, RP04_DEV, MBA_RP_CTRL, "RP04" },
    { RM80_SECT, RM80_SURF, RM80_CYL, RM80_SIZE, RM80_DEV, MBA_RM_CTRL, "RM80" },
    { RP06_SECT, RP06_SURF, RP06_CYL, RP06_SIZE, RP06_DEV, MBA_RP_CTRL, "RP06" },
    { RM05_SECT, RM05_SURF, RM05_CYL, RM05_SIZE, RM05_DEV, MBA_RM_CTRL, "RM05" },
    { RP07_SECT, RP07_SURF, RP07_CYL, RP07_SIZE, RP07_DEV, MBA_RM_CTRL, "RP07" },
    { 0 }
    };

#define DBG_DSK 0x0001                                  /* display sim_disk activities */

DEBTAB rp_debug[] = {
    {"DISK",    DBG_DSK, "display sim_disk activities" },
    {0}
};
extern int32 ubmap[UBANUM][UMAP_MEMSIZE];               /* Unibus maps */
extern int32 ubcs[UBANUM];
extern uint32 fe_bootrh;
extern int32 fe_bootunit;

int32 rpcs1 = 0;                                        /* control/status 1 */
int32 rpwc = 0;                                         /* word count */
int32 rpba = 0;                                         /* bus address */
int32 rpcs2 = 0;                                        /* control/status 2 */
int32 rpdb = 0;                                         /* data buffer */
uint16 rpda[RP_NUMDR] = { 0 };                          /* track/sector */
uint16 rpds[RP_NUMDR] = { 0 };                          /* drive status */
uint16 rper1[RP_NUMDR] = { 0 };                         /* error status 1 */
uint16 rmhr[RP_NUMDR] = { 0 };                          /* holding reg */
uint16 rpmr[RP_NUMDR] = { 0 };                          /* maint reg */
uint16 rmmr2[RP_NUMDR] = { 0 };                         /* maint reg 2 */
uint16 rpof[RP_NUMDR] = { 0 };                          /* offset */
uint16 rpdc[RP_NUMDR] = { 0 };                          /* cylinder */
uint16 rper2[RP_NUMDR] = { 0 };                         /* error status 2 */
uint16 rper3[RP_NUMDR] = { 0 };                         /* error status 3 */
uint16 rpec1[RP_NUMDR] = { 0 };                         /* ECC correction 1 */
uint16 rpec2[RP_NUMDR] = { 0 };                         /* ECC correction 2 */
int32 rpiff = 0;                                        /* INTR flip/flop */
int32 rp_stopioe = 1;                                   /* stop on error */
int32 rp_swait = 10;                                    /* seek time */
int32 rp_rwait = 10;                                    /* rotate time */
static int32 reg_in_drive[32] = {
    0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

t_stat rp_rd (int32 *data, int32 PA, int32 access);
t_stat rp_wr (int32 data, int32 PA, int32 access);
int32 rp_inta (void);
t_stat rp_svc (UNIT *uptr);
t_stat rp_reset (DEVICE *dptr);
t_stat rp_boot (int32 unitno, DEVICE *dptr);
t_stat rp_attach (UNIT *uptr, CONST char *cptr);
t_stat rp_detach (UNIT *uptr);
void set_rper (int16 flag, int32 drv);
void update_rpcs (int32 flags, int32 drv);
void rp_go (int32 drv, int32 fnc);
t_stat rp_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rp_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat rp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *rp_description (DEVICE *dptr);

/* RP data structures

   rp_dev       RP device descriptor
   rp_unit      RP unit list
   rp_reg       RP register list
   rp_mod       RP modifier list
*/

DIB rp_dib = {
    IOBA_RP, IOLN_RP, &rp_rd, &rp_wr,
    1, IVCL (RP), VEC_RP, { &rp_inta }, IOLN_RP
    };

UNIT rp_unit[] = {
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) }
    };

REG rp_reg[] = {
    { ORDATAD (RPCS1, rpcs1, 16, "control status 1") },
    { ORDATAD (RPWC, rpwc, 16, "word count") },
    { ORDATAD (RPBA, rpba, 16, "bus address") },
    { ORDATAD (RPCS2, rpcs2, 16, "control status") },
    { ORDATAD (RPDB, rpdb, 16, "data buffer") },
    { BRDATAD (RPDA, rpda, 8, 16, RP_NUMDR, "desired surface, sector") },
    { BRDATAD (RPDS, rpds, 8, 16, RP_NUMDR, "drive status, drives 0 to 7") },
    { BRDATAD (RPER1, rper1, 8, 16, RP_NUMDR, "drive errors, drives 0 to 7") },
    { BRDATAD (RPHR, rmhr, 8, 16, RP_NUMDR, "holding register, drives 0 to 7") },
    { BRDATAD (RPOF, rpof, 8, 16, RP_NUMDR, "offset, drives 0 to 7") },
    { BRDATAD (RPDC, rpdc, 8, 16, RP_NUMDR, "desired cylinder, drives 0 to 7") },
    { BRDATAD (RPER2, rper2, 8, 16, RP_NUMDR, "error status 2, drives 0 to 7") },
    { BRDATAD (RPER3, rper3, 8, 16, RP_NUMDR, "error status 3, drives 0 to 7") },
    { BRDATAD (RPEC1, rpec1, 8, 16, RP_NUMDR, "ECC syndrome 1, drives 0 to 7") },
    { BRDATAD (RPEC2, rpec2, 8, 16, RP_NUMDR, "ECC syndrome 2, drives 0 to 7") },
    { BRDATAD (RMMR, rpmr, 8, 16, RP_NUMDR, "maintenance register, drives 0 to 7") },
    { BRDATAD (RMMR2, rmmr2, 8, 16, RP_NUMDR, "maintenance register 2, drives 0 to 7") },
    { FLDATAD (IFF, rpiff, 0, "transfer complete interrupt request flop") },
    { FLDATAD (INT, int_req, INT_V_RP, "interrupt pending flag") },
    { FLDATAD (SC, rpcs1, CSR_V_ERR, "special condition (CSR1<15>)") },
    { FLDATAD (DONE, rpcs1, CSR_V_DONE, "device done flag (CSR1<7>)") },
    { FLDATAD (IE, rpcs1, CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (STIME, rp_swait, 24, "seek time, per cylinder"), REG_NZ + PV_LEFT },
    { DRDATAD (RTIME, rp_rwait, 24, "rotational delay"), REG_NZ + PV_LEFT },
    { URDATA (FNC, rp_unit[0].FUNC, 8, 5, 0, RP_NUMDR, REG_HRO) },
    { URDATA (CAPAC, rp_unit[0].capac, 10, T_ADDR_W, 0,
              RP_NUMDR, PV_LEFT | REG_HRO) },
    { FLDATAD (STOP_IOE, rp_stopioe, 0, "stop on I/O error") },
    { NULL }
    };

MTAB rp_mod[] = {
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED",
      NULL, NULL, NULL, "Write enable disk drive" },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED",
      NULL, NULL, NULL, "Write lock disk drive" },
    { MTAB_XTD|MTAB_VUN, RM03_DTYPE, NULL, "RM03",
      &rp_set_type, NULL, NULL, "Set RM03 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RP04_DTYPE, NULL, "RP04",
      &rp_set_type, NULL, NULL, "Set RP04 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RM80_DTYPE, NULL, "RM80",
      &rp_set_type, NULL, NULL, "Set RM80 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RP06_DTYPE, NULL, "RP06",
      &rp_set_type, NULL, NULL, "Set RP06 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RM05_DTYPE, NULL, "RM05",
      &rp_set_type, NULL, NULL, "Set RM05 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RP07_DTYPE, NULL, "RP07",
      &rp_set_type, NULL, NULL, "Set RP07 Disk Type" },
    { MTAB_XTD|MTAB_VUN, 0, "TYPE", NULL,
      NULL, &rp_show_type, NULL, "Display device type" },
    { UNIT_AUTO, UNIT_AUTO, "autosize", "AUTOSIZE", 
      NULL, NULL, NULL, "Set type based on file size at attach" },
    { UNIT_AUTO,         0, "noautosize",   "NOAUTOSIZE",   
      NULL, NULL, NULL, "Disable disk autosize on attach" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "FORMAT", "FORMAT={AUTO|SIMH|VHD|RAW}",
      &sim_disk_set_fmt, &sim_disk_show_fmt, NULL, "Display disk format" },
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
      NULL, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
      NULL, &show_vec, NULL },
    { 0 }
    };

DEVICE rp_dev = {
    "RP", rp_unit, rp_reg, rp_mod,
    RP_NUMDR, 8, 30, 1, 8, 36,
    NULL, NULL, &rp_reset,
    &rp_boot, &rp_attach, &rp_detach,
    &rp_dib, DEV_UBUS | DEV_DEBUG | DEV_DISK,
    0, rp_debug, NULL, NULL, &rp_help, sim_disk_attach_help, NULL,
    &rp_description
    };

/* I/O dispatch routines, I/O addresses 17776700 - 17776776 */

t_stat rp_rd (int32 *data, int32 PA, int32 access)
{
int32 drv, dtype, i, j;

drv = GET_UNIT (rpcs2);                                 /* get current unit */
dtype = GET_DTYPE (rp_unit[drv].flags);                 /* get drive type */
j = (PA >> 1) & 037;                                    /* get reg offset */
if (reg_in_drive[j] && (rp_unit[drv].flags & UNIT_DIS)) { /* nx disk */
    rpcs2 = rpcs2 | CS2_NED;                            /* set error flag */
    update_rpcs (CS1_SC, drv);                          /* request intr */
    *data = 0;
    return SCPE_OK;
    }

update_rpcs (0, drv);                                   /* update status */
switch (j) {                                            /* decode PA<5:1> */

    case 000:                                           /* RPCS1 */
        *data = rpcs1;
        break;

    case 001:                                           /* RPWC */
        *data = rpwc;
        break;

    case 002:                                           /* RPBA */
        *data = rpba = rpba & ~BA_MBZ;
        break;

    case 003:                                           /* RPDA */
        *data = rpda[drv] = rpda[drv] & ~DA_MBZ;
        break;

    case 004:                                           /* RPCS2 */
        *data = rpcs2 = (rpcs2 & ~CS2_MBZ) | CS2_IR | CS2_OR;
        break;

    case 005:                                           /* RPDS */
        *data = rpds[drv];
        break;

    case 006:                                           /* RPER1 */
        *data = rper1[drv];
        break;

    case 007:                                           /* RPAS */
        *data = 0;
        for (i = 0; i < RP_NUMDR; i++)
            if (rpds[i] & DS_ATA)
                *data = *data | (AS_U0 << i);
        break;

    case 010:                                           /* RPLA */
        *data = GET_SECTOR (rp_rwait, dtype) << LA_V_SC;
        break;

    case 011:                                           /* RPDB */
        *data = rpdb;
        break;

    case 012:                                           /* RPMR */
        *data = rpmr[drv];
        break;

    case 013:                                           /* RPDT */
        *data = drv_tab[dtype].devtype;
        break;

    case 014:                                           /* RPSN */
        *data = 020 | (drv + 1);
        break;

    case 015:                                           /* RPOF */
        *data = rpof[drv] = rpof[drv] & ~OF_MBZ;
        break;

    case 016:                                           /* RPDC */
        *data = rpdc[drv] = rpdc[drv] & ~DC_MBZ;
        break;

    case 017:                                           /* RPCC, RMHR */
        if (drv_tab[dtype].ctrl == MBA_RP_CTRL)         /* RP is CC */
            *data = rp_unit[drv].CYL;
        else *data = rmhr[drv] ^ 0177777;               /* RM is HR */
        break;

    case 020:                                           /* RPER2, RMMR2 */
        if (drv_tab[dtype].ctrl == MBA_RP_CTRL)         /* RP is ER2 */
            *data = rper2[drv];
        else *data = rmmr2[drv];                        /* RM is MR2 */
        break;

    case 021:                                           /* RPER3, RMER2 */
        if (drv_tab[dtype].ctrl == MBA_RP_CTRL)         /* RP is ER3 */
            *data = rper3[drv];
        else *data = rper2[drv];                        /* RM is ER2 */
        break;

    case 022:                                           /* RPEC1 */
        *data = rpec1[drv];
        break;

    case 023:                                           /* RPEC2 */
        *data = rpec2[drv];
        break;

    default:                                            /* all others */
        set_rper (ER1_ILR, drv);
        update_rpcs (0, drv);
        break;
        }
return SCPE_OK;
}

t_stat rp_wr (int32 data, int32 PA, int32 access)
{
int32 cs1f, drv, i, j;
UNIT *uptr;

cs1f = 0;                                               /* no int on cs1 upd */
drv = GET_UNIT (rpcs2);                                 /* get current unit */
uptr = rp_dev.units + drv;                              /* get unit */
j = (PA >> 1) & 037;                                    /* get reg offset */
if (reg_in_drive[j] && (rp_unit[drv].flags & UNIT_DIS)) { /* nx disk */
    rpcs2 = rpcs2 | CS2_NED;                            /* set error flag */
    update_rpcs (CS1_SC, drv);                          /* request intr */
    return SCPE_OK;
    }
if (reg_in_drive[j] && sim_is_active (uptr) && (uptr->flags & UNIT_UTS)) { /* unit busy? */
    set_rper (ER1_RMR, drv);                            /* won't write */
    update_rpcs (0, drv);
    return SCPE_OK;
    }
rmhr[drv] = (uint16)data;

switch (j) {                                            /* decode PA<5:1> */

    case 000:                                           /* RPCS1 */
        if ((access == WRITEB) && (PA & 1))
            data = data << 8;
        if (data & CS1_TRE) {                           /* error clear? */
            rpcs1 = rpcs1 & ~CS1_TRE;                   /* clr CS1<TRE> */
            rpcs2 = rpcs2 & ~CS2_ERR;                   /* clr CS2<15:8> */
            }
        if ((access == WRITE) || (PA & 1)) {            /* hi byte write? */
            if (rpcs1 & CS1_DONE)                       /* done set? */
                rpcs1 = (rpcs1 & ~CS1_UAE) | (data & CS1_UAE);
            }
        if ((access == WRITE) || !(PA & 1)) {           /* lo byte write? */
            if ((data & CS1_DONE) && (data & CS1_IE))   /* to DONE+IE? */
                rpiff = 1;                              /* set CSTB INTR */
            rpcs1 = (rpcs1 & ~CS1_IE) | (data & CS1_IE);
            if (uptr->flags & UNIT_DIS) {               /* nx disk? */
                rpcs2 = rpcs2 | CS2_NED;                /* set error flag */
                cs1f = CS1_SC;                          /* req interrupt */
                }
            else if (sim_is_active (uptr) && (uptr->flags & UNIT_UTS))
                set_rper (ER1_RMR, drv);                /* won't write */
            else if (data & CS1_GO) {                   /* start op */
                uptr->FUNC = GET_FNC (data);            /* set func */
                if ((uptr->FUNC >= FNC_XFER) &&         /* data xfer and */
                   ((rpcs1 & CS1_DONE) == 0))           /* ~rdy? PGE */
                    rpcs2 = rpcs2 | CS2_PGE;
                else rp_go (drv, uptr->FUNC);
                }
            }
        break;  

    case 001:                                           /* RPWC */
        if (access == WRITEB)
            data = (PA & 1)?
                   (rpwc & 0377) | (data << 8): (rpwc & ~0377) | data;
        rpwc = data;
        break;

    case 002:                                           /* RPBA */
        if (access == WRITEB)
            data = (PA & 1)?
                   (rpba & 0377) | (data << 8): (rpba & ~0377) | data;
        rpba = data & ~BA_MBZ;
        break;

    case 003:                                           /* RPDA */
        if ((access == WRITEB) && (PA & 1))
            data = data << 8;
        rpda[drv] = (uint16)(data & ~DA_MBZ);
        break;

    case 004:                                           /* RPCS2 */
        if ((access == WRITEB) && (PA & 1))
            data = data << 8;
        if (data & CS2_CLR)                             /* init? */
            rp_reset (&rp_dev);
        else {
            if ((data & ~rpcs2) & (CS2_PE | CS2_MXF))
                cs1f = CS1_SC;                          /* diagn intr */
            if (access == WRITEB)                       /* merge data */
                data = (rpcs2 & ((PA & 1)? 0377: 0177400)) | data;
            rpcs2 = (rpcs2 & ~CS2_RW) | (data & CS2_RW) | CS2_IR | CS2_OR;
            }
        drv = GET_UNIT (rpcs2);
        break;

    case 006:                                           /* RPER1 */
        if ((access == WRITEB) && (PA & 1))
            data = data << 8;
        rper1[drv] = (uint16)data;
        break;

    case 007:                                           /* RPAS */
        if ((access == WRITEB) && (PA & 1))
            break;
        for (i = 0; i < RP_NUMDR; i++)
            if (data & (AS_U0 << i))
                rpds[i] = rpds[i] & ~DS_ATA;
        break;

    case 011:                                           /* RPDB */
        if (access == WRITEB)
            data = (PA & 1)?
                   (rpdb & 0377) | (data << 8): (rpdb & ~0377) | data;
        rpdb = data;
        break;

    case 012:                                           /* RPMR */
        if ((access == WRITEB) && (PA & 1))
            data = data << 8;
        rpmr[drv] = (uint16)data;
        break;

    case 015:                                           /* RPOF */
        rpof[drv] = (uint16)(data & ~OF_MBZ);
        break;

    case 016:                                           /* RPDC */
        if ((access == WRITEB) && (PA & 1))
            data = data << 8;
        rpdc[drv] = (uint16)(data & ~DC_MBZ);
        break;

    case 005:                                           /* RPDS */
    case 010:                                           /* RPLA */
    case 013:                                           /* RPDT */
    case 014:                                           /* RPSN */
    case 017:                                           /* RPCC, RMHR */
    case 020:                                           /* RPER2, RMMR2 */
    case 021:                                           /* RPER3, RMER2 */
    case 022:                                           /* RPEC1 */
    case 023:                                           /* RPEC2 */
        break;                                          /* read only */

    default:                                            /* all others */
        set_rper (ER1_ILR, drv);
        break;
        }                                               /* end switch */

update_rpcs (cs1f, drv);                                /* update status */
return SCPE_OK;
}

/* Initiate operation - unit not busy, function set */

void rp_go (int32 drv, int32 fnc)
{
int32 dc, dtype, t;
UNIT *uptr;

uptr = rp_dev.units + drv;                              /* get unit */
if (uptr->flags & UNIT_DIS) {                           /* nx unit? */
    rpcs2 = rpcs2 | CS2_NED;                            /* set error flag */
    update_rpcs (CS1_SC, drv);                          /* request intr */
    return;
    }
if ((fnc != FNC_DCLR) && (rpds[drv] & DS_ERR)) {        /* err & ~clear? */
    set_rper (ER1_ILF, drv);                            /* set err, ATN */
    update_rpcs (CS1_SC, drv);                          /* request intr */
    return;
    }
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
rpds[drv] = rpds[drv] & ~DS_ATA;                        /* clear attention */
dc = rpdc[drv];                                         /* assume seek, sch */

switch (fnc) {                                          /* case on function */

    case FNC_DCLR:                                      /* drive clear */
        rper1[drv] = rper2[drv] = rper3[drv] = 0;       /* clear errors */
        rpec2[drv] = 0;                                 /* clear EC2 */
        if (drv_tab[dtype].ctrl == MBA_RM_CTRL)         /* RM? */
            rpmr[drv] = 0;                              /* clear maint */
        else rpec1[drv] = 0;                            /* RP, clear EC1 */
    case FNC_NOP:                                       /* no operation */
    case FNC_RELEASE:                                   /* port release */
        return;

    case FNC_PRESET:                                    /* read-in preset */
        rpdc[drv] = 0;                                  /* clear disk addr */
        rpda[drv] = 0;
        rpof[drv] = 0;                                  /* clear offset */
    case FNC_PACK:                                      /* pack acknowledge */
        if ((uptr->flags & UNIT_UTS) == 0) {            /* not attached? */
            set_rper (ER1_UNS, drv);                    /* unsafe */
            break;
            }
        rpds[drv] = rpds[drv] | DS_VV;                  /* set volume valid */
        return;

    case FNC_OFFSET:                                    /* offset mode */
    case FNC_RETURN:
        if ((uptr->flags & UNIT_UTS) == 0) {            /* not attached? */
            set_rper (ER1_UNS, drv);                    /* unsafe */
            break;
            }
        rpds[drv] = (rpds[drv] & ~DS_RDY) | DS_PIP;     /* set positioning */
        sim_activate (uptr, rp_swait);                  /* time operation */
        return;

    case FNC_UNLOAD:                                    /* unload */
    case FNC_RECAL:                                     /* recalibrate */
        dc = 0;                                         /* seek to 0 */
    case FNC_SEEK:                                      /* seek */
    case FNC_SEARCH:                                    /* search */
        if ((uptr->flags & UNIT_UTS) == 0) {            /* not attached? */
            set_rper (ER1_UNS, drv);                    /* unsafe */
            break;
            }
        if ((GET_CY (dc) >= drv_tab[dtype].cyl) ||      /* bad cylinder */
            (GET_SF (rpda[drv]) >= drv_tab[dtype].surf) || /* bad surface */
            (GET_SC (rpda[drv]) >= drv_tab[dtype].sect)) { /* or bad sector? */
            set_rper (ER1_IAE, drv);
            break;
            }
        rpds[drv] = (rpds[drv] & ~DS_RDY) | DS_PIP;     /* set positioning */
        t = abs (dc - uptr->CYL);                       /* cyl diff */
        if (t == 0)                                     /* min time */
            t = 1;
        sim_activate (uptr, rp_swait * t);              /* schedule */
        uptr->CYL = dc;                                 /* save cylinder */
        return;

    case FNC_WRITEH:                                    /* write headers */
    case FNC_WRITE:                                     /* write */
    case FNC_WCHK:                                      /* write check */
    case FNC_READ:                                      /* read */
    case FNC_READH:                                     /* read headers */
        if ((uptr->flags & UNIT_UTS) == 0) {            /* not attached? */
            set_rper (ER1_UNS, drv);                    /* unsafe */
            break;
            }
        rpcs2 = rpcs2 & ~CS2_ERR;                       /* clear errors */
        rpcs1 = rpcs1 & ~(CS1_TRE | CS1_MCPE | CS1_DONE);
        if ((GET_CY (dc) >= drv_tab[dtype].cyl) ||      /* bad cylinder */
            (GET_SF (rpda[drv]) >= drv_tab[dtype].surf) || /* bad surface */
            (GET_SC (rpda[drv]) >= drv_tab[dtype].sect)) { /* or bad sector? */
            set_rper (ER1_IAE, drv);
            break;
            }
        rpds[drv] = rpds[drv] & ~DS_RDY;                /* clear drive rdy */
        sim_activate (uptr, rp_rwait + (rp_swait * abs (dc - uptr->CYL)));
        uptr->CYL = dc;                                 /* save cylinder */
        return;

    default:                                            /* all others */
        set_rper (ER1_ILF, drv);                        /* not supported */
        break;
        }

update_rpcs (CS1_SC, drv);                              /* req intr */
return;
}

/* Service unit timeout

   Complete movement or data transfer command
   Unit must exist - can't remove an active unit
   Unit must be attached - detach cancels in progress operations
*/

t_stat rp_svc (UNIT *uptr)
{
int32 i, dtype, drv;
int32 ba, da, vpn;
a10 pa10, mpa10;
int32 wc10, twc10, awc10, fc10;
static d10 dbuf[RP_MAXFR];
t_stat r;

dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
drv = (int32) (uptr - rp_dev.units);                    /* get drv number */
if ((uptr->flags & UNIT_UTS) == 0) {                    /* Transition to up-to-speed */
    uptr->flags |= UNIT_UTS;
    rpds[drv] = DS_ATA | DS_MOL | DS_DPR | DS_RDY |
               ((uptr->flags & UNIT_WPRT)? DS_WRL: 0);
    update_rpcs (CS1_SC, drv);
    return SCPE_OK;
    }
rpds[drv] = (rpds[drv] & ~DS_PIP) | DS_RDY;             /* change drive status */

switch (uptr->FUNC) {                                   /* case on function */

    case FNC_OFFSET:                                    /* offset */
        rpds[drv] = rpds[drv] | DS_OF | DS_ATA;         /* set offset, attention */
        update_rpcs (CS1_SC, drv);
        break;

    case FNC_RETURN:                                    /* return to centerline */
        rpds[drv] = (rpds[drv] & ~DS_OF) | DS_ATA;      /* clear offset, set attn */
        update_rpcs (CS1_SC, drv);
        break;  

    case FNC_UNLOAD:                                    /* unload */
        rp_detach (uptr);                               /* detach unit */
        rpds[drv] &= ~DS_ATA;                           /* Unload does not interrupt */
        update_rpcs (0, drv);
        break;

    case FNC_RECAL:                                     /* recalibrate */
    case FNC_SEARCH:                                    /* search */
    case FNC_SEEK:                                      /* seek */
        rpds[drv] = rpds[drv] | DS_ATA;                 /* set attention */
        update_rpcs (CS1_SC, drv);
        break;

/* Reads and writes must take into account the complicated relationship
   between Unibus addresses and PDP-10 memory addresses, and Unibus
   byte and word counts, PDP-10 UBA word counts, and simulator PDP-10
   word counts (due to the fact that the simulator must transfer eight
   8b bytes to do a 36b transfer, whereas the UBA did four 9b bytes).
*/

#define XWC_MBZ         0000001                         /* wc<0> must be 0 */
#define XBA_MBZ         0000003                         /* addr<1:0> must be 0 */

    case FNC_WRITE:                                     /* write */
        if (uptr->flags & UNIT_WPRT) {                  /* write locked? */
            set_rper (ER1_WLE, drv);                    /* set drive error */
            update_rpcs (CS1_DONE | CS1_TRE, drv);      /* set done, err */
            break;
            }
        /* fall through */
    case FNC_WCHK:                                      /* write check */
    case FNC_READ:                                      /* read */
    case FNC_READH:                                     /* read headers */
        ba = GET_UAE (rpcs1) | rpba;                    /* get byte addr */
        wc10 = (0200000 - rpwc) >> 1;                   /* get PDP-10 wc */
        da = GET_DA (rpdc[drv], rpda[drv], dtype) * RP_NUMWD; /* get disk addr */
        if ((da + wc10) > drv_tab[dtype].size) {        /* disk overrun? */
            set_rper (ER1_AOE, drv);
            if (wc10 > (drv_tab[dtype].size - da))
                wc10 = drv_tab[dtype].size - da;
            }

        if (uptr->FUNC == FNC_WRITE) {                  /* write? */
            for (twc10 = 0; twc10 < wc10; twc10++) {
                pa10 = ba >> 2;
                vpn = PAG_GETVPN (pa10);                /* map addr */
                if ((vpn >= UMAP_MEMSIZE) || (ba & XBA_MBZ) || (rpwc & XWC_MBZ) ||
                  ((ubmap[0][vpn] & (UMAP_VLD | UMAP_DSB | UMAP_RRV)) != UMAP_VLD)) {
                    rpcs2 = rpcs2 | CS2_NEM;            /* set error */
                    ubcs[0] = ubcs[0] | UBCS_TMO;       /* UBA times out */
                    break;
                    }
                mpa10 = (ubmap[0][vpn] + PAG_GETOFF (pa10)) & PAMASK;
                if (MEM_ADDR_NXM (mpa10)) {             /* nx memory? */
                    rpcs2 = rpcs2 | CS2_NEM;            /* set error */
                    ubcs[0] = ubcs[0] | UBCS_TMO;       /* UBA times out */
                    break;
                    }
                dbuf[twc10] = M[mpa10];                 /* write to disk */
                if ((rpcs2 & CS2_UAI) == 0)
                    ba = ba + 4;
                }
            if ((fc10 = twc10 & (RP_NUMWD - 1))) {      /* fill? */
                fc10 = RP_NUMWD - fc10;
                for (i = 0; i < fc10; i++)
                    dbuf[twc10 + i] = 0;
                }
            r = sim_disk_wrsect (uptr, da/RP_NUMWD, (uint8 *)dbuf,
                                 NULL, (twc10 + fc10 + RP_NUMWD - 1)/RP_NUMWD);
            }                                           /* end if */
        else {                                          /* read, wchk, readh */
            t_seccnt sectsread;

            r = sim_disk_rdsect (uptr, da/RP_NUMWD, (uint8 *)dbuf,
                                 &sectsread, (wc10 + RP_NUMWD - 1)/RP_NUMWD);
            awc10 = sectsread * RP_NUMWD;
            for ( ; awc10 < wc10; awc10++)
                dbuf[awc10] = 0;
            for (twc10 = 0; twc10 < wc10; twc10++) {
                pa10 = ba >> 2;
                vpn = PAG_GETVPN (pa10);                /* map addr */
                if ((vpn >= UMAP_MEMSIZE) || (ba & XBA_MBZ) || (rpwc & XWC_MBZ) ||
                  ((ubmap[0][vpn] & (UMAP_VLD | UMAP_DSB | UMAP_RRV)) != UMAP_VLD)) {
                    rpcs2 = rpcs2 | CS2_NEM;            /* set error */
                    ubcs[0] = ubcs[0] | UBCS_TMO;       /* UBA times out */
                    break;
                    }
                mpa10 = (ubmap[0][vpn] + PAG_GETOFF (pa10)) & PAMASK;
                if (MEM_ADDR_NXM (mpa10)) {             /* nx memory? */
                    rpcs2 = rpcs2 | CS2_NEM;            /* set error */
                    ubcs[0] = ubcs[0] | UBCS_TMO;       /* UBA times out */
                    break;
                    }
                if ((uptr->FUNC == FNC_READ) ||         /* read or */
                    (uptr->FUNC == FNC_READH))          /* read header */
                     M[mpa10] = dbuf[twc10];
                else if (M[mpa10] != dbuf[twc10]) {     /* wchk, mismatch? */
                     rpcs2 = rpcs2 | CS2_WCE;           /* set error */
                     break;
                     }
                if ((rpcs2 & CS2_UAI) == 0)
                    ba = ba + 4;
                }
            }                                           /* end else */

        rpwc = (rpwc + (twc10 << 1)) & 0177777;         /* final word count */
        rpba = (ba & 0177777) & ~BA_MBZ;                /* lower 16b */
        rpcs1 = (rpcs1 & ~ CS1_UAE) | ((ba >> (16 - CS1_V_UAE)) & CS1_UAE);
        da = da + twc10 + (RP_NUMWD - 1);
        if (da >= drv_tab[dtype].size)
            rpds[drv] = rpds[drv] | DS_LST;
        da = da / RP_NUMWD;
        rpda[drv] = (uint16)(da % drv_tab[dtype].sect);
        da = da / drv_tab[dtype].sect;
        rpda[drv] = (uint16)(rpda[drv] | ((da % drv_tab[dtype].surf) << DA_V_SF));
        rpdc[drv] = (uint16)(da / drv_tab[dtype].surf);

        if (r != SCPE_OK) {                             /* error? */
            set_rper (ER1_PAR, drv);                    /* set drive error */
            update_rpcs (CS1_DONE | CS1_TRE, drv);      /* set done, err */
            sim_printf ("RP I/O error");
            return SCPE_IOERR;
            }
        /* fall through */

    case FNC_WRITEH:                                    /* write headers stub */
        update_rpcs (CS1_DONE, drv);                    /* set done */
        break;
        }                                               /* end case func */

return SCPE_OK;
}

/* Set drive error */

void set_rper (int16 flag, int32 drv)
{
rper1[drv] = rper1[drv] | flag;
rpds[drv] = rpds[drv] | DS_ATA;
rpcs1 = rpcs1 | CS1_SC;
return;
}

/* Controller status update

   Check for done transition
   Update drive status
   Update RPCS1
   Update interrupt request
*/

void update_rpcs (int32 flag, int32 drv)
{
int32 i;
UNIT *uptr;

if ((flag & ~rpcs1) & CS1_DONE)                         /* DONE 0 to 1? */
    rpiff = (rpcs1 & CS1_IE)? 1: 0;                     /* CSTB INTR <- IE */
uptr = rp_dev.units + drv;                              /* get unit */
if (rp_unit[drv].flags & UNIT_DIS)
    rpds[drv] = rper1[drv] = 0;
else rpds[drv] = (rpds[drv] | DS_DPR) & ~DS_PGM;
if (rp_unit[drv].flags & UNIT_UTS)
    rpds[drv] = rpds[drv] | DS_MOL;
else rpds[drv] = rpds[drv] & ~(DS_MOL | DS_VV | DS_RDY);
if (rper1[drv] | rper2[drv] | rper3[drv])
    rpds[drv] = rpds[drv] | DS_ERR;
else rpds[drv] = rpds[drv] & ~DS_ERR;

rpcs1 = (rpcs1 & ~(CS1_SC | CS1_MCPE | CS1_MBZ | CS1_DRV)) | CS1_DVA | flag;
rpcs1 = rpcs1 | (uptr->FUNC << CS1_V_FNC);
if (sim_is_active (uptr) && (uptr->flags & UNIT_UTS))
    rpcs1 = rpcs1 | CS1_GO;
if (rpcs2 & CS2_ERR)
    rpcs1 = rpcs1 | CS1_TRE | CS1_SC;
else if (rpcs1 & CS1_TRE)
    rpcs1 = rpcs1 | CS1_SC;
for (i = 0; i < RP_NUMDR; i++) {
    if (rpds[i] & DS_ATA) {
        rpcs1 = rpcs1 | CS1_SC;
        break;
        }
    }
if (rpiff || ((rpcs1 & CS1_SC) && (rpcs1 & CS1_DONE) && (rpcs1 & CS1_IE)))
    int_req = int_req | INT_RP;
else int_req = int_req & ~INT_RP;
return;
}

/* Interrupt acknowledge */

int32 rp_inta (void)
{
rpcs1 = rpcs1 & ~CS1_IE;                                /* clear int enable */
rpiff = 0;                                              /* clear CSTB INTR */
return VEC_RP;                                          /* acknowledge */
}

/* Device reset */

t_stat rp_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

rpcs1 = CS1_DVA | CS1_DONE;
rpcs2 = CS2_IR | CS2_OR;
rpba = rpwc = 0;
rpiff = 0;                                              /* clear CSTB INTR */
int_req = int_req & ~INT_RP;                            /* clear intr req */
for (i = 0; i < RP_NUMDR; i++) {
    uptr = rp_dev.units + i;
    uptr->CYL = uptr->FUNC = 0;
    if (uptr->flags & UNIT_ATT)
        if (uptr->flags & UNIT_UTS) {
            sim_cancel (uptr);
            rpds[i] = (rpds[i] & DS_VV) | DS_DPR | DS_RDY | DS_MOL |
                       ((uptr->flags & UNIT_WPRT)? DS_WRL: 0);
            } else {
            if (!sim_is_active (uptr))
                sim_activate_after (uptr, SPINUP_DLY);
            rpds[i] = DS_DPR | ((uptr->flags & UNIT_WPRT)? DS_WRL: 0);
            }
    else {
        sim_cancel (uptr);
        if (uptr->flags & UNIT_DIS)
            rpds[i] = 0;
        else rpds[i] = DS_DPR;
    }
    rper1[i] = 0;
    rper2[i] = 0;
    rper3[i] = 0;
    rpda[i] = 0;
    rpdc[i] = 0;
    rpmr[i] = 0;
    rpof[i] = 0;
    rpec1[i] = 0;
    rpec2[i] = 0;
    rmmr2[i] = 0;
    rmhr[i] = 0;
    }
return SCPE_OK;
}

/* Device attach */

t_stat rp_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;
static const char *drives[] = {"RM03", "RP04", "RM80", "RP06", "RM05", "RP07", NULL};

uptr->capac = drv_tab[GET_DTYPE (uptr->flags)].size;
r = sim_disk_attach_ex (uptr, cptr, RP_NUMWD * sizeof (d10), sizeof (d10), TRUE, DBG_DSK,
                           drv_tab[GET_DTYPE (uptr->flags)].name,
                           0, 0, (uptr->flags & UNIT_AUTO) ? drives : NULL);
if (r != SCPE_OK)
    return r;
sim_cancel (uptr);
uptr->flags &= ~UNIT_UTS;
sim_activate_after (uptr, SPINUP_DLY);
return SCPE_OK;
}

/* Device detach */

t_stat rp_detach (UNIT *uptr)
{
int32 drv;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
drv = (int32) (uptr - rp_dev.units);                    /* get drv number */
rpds[drv] = (rpds[drv] & ~(DS_MOL | DS_RDY | DS_WRL | DS_VV | DS_OF)) |
    DS_ATA;
if (sim_is_active (uptr)) {                             /* unit active? */
    sim_cancel (uptr);                                  /* cancel operation */
    if (uptr->flags & UNIT_UTS) {
        rper1[drv] = rper1[drv] | ER1_OPI;              /* set drive error */
        if (uptr->FUNC >= FNC_WCHK)                     /* data transfer? */
            rpcs1 = rpcs1 | CS1_DONE | CS1_TRE;         /* set done, err */
        }
    }
uptr->flags &= ~UNIT_UTS;
update_rpcs (0, drv);                                  /* request intr */
return sim_disk_detach (uptr);
}

/* Set type command validation routine */

t_stat rp_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if ((val < 0) || (cptr && *cptr))
    return SCPE_ARG;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->flags = (uptr->flags & ~UNIT_DTYPE) | (val << UNIT_V_DTYPE);
uptr->capac = (t_addr)drv_tab[val].size;
return SCPE_OK;
}

/* Show unit type */

t_stat rp_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "%s", drv_tab[GET_DTYPE (uptr->flags)].name);
return SCPE_OK;
}

/* Device bootstrap
 * The DEC and ITS versions are word-for-word identical, except that
 * the DEC RDIO/WRIO are replaced by IORDQ and IOWRQ.  This is hand
 * assembled code, so please always make changes in both.
 * Due to a typo in the KS Console rom, block 010 is read for the
 * alternate HOM block.  The correct block is 012.  For compatibiliy,
 * we will do what the hardware did first, what's right if it fails (as it will).
 */

#define BOOT_START      0377000                         /* start */
#define BOOT_LEN (sizeof (boot_rom_dec) / sizeof (d10))

static const d10 boot_rom_dec[] = {
    INT64_C(0510040000000)+FE_RHBASE,       /* boot:hllz 1,FE_RHBASE   ; uba # */
    INT64_C(0201000140001),                 /*      movei 0,140001  ; vld,fst,pg 1 */
    INT64_C(0713001000000)+((IOBA_UBMAP+1) & RMASK),   /*      wrio 0,763001(1); set ubmap */
    INT64_C(0200040000000)+FE_RHBASE,       /*      move 1,FE_RHBASE */
    INT64_C(0201000000040),                 /*      movei 0,40      ; ctrl reset */
    INT64_C(0713001000010),                 /*      wrio 0,10(1)    ; ->RPCS2 */
    INT64_C(0200240000000)+FE_UNIT,         /*      move 5,FE_UNIT  ; unit */
    INT64_C(0713241000010),                 /*      wrio 5,10(1)    ; select ->RPCS2 */

    INT64_C(0712001000012),                 /*10    rdio 0,12(1)    ; RPDS */
    INT64_C(0640000010600),                 /*      trc  0,10600    ; MOL + DPR + RDY */
    INT64_C(0642000010600),                 /*      trce 0,10600    ; */
    INT64_C(0254000377010),                 /*      jrst .-3        ; wait */
    INT64_C(0201000000377),                 /*      movei 0,377     ; All units */
    INT64_C(0713001000016),                 /*      wrio 0,16(1)    ; Clear on-line attns */
    INT64_C(0201000000021),                 /*      movei 0,21      ; preset */
    INT64_C(0713001000000),                 /*      wrio 0,0(1)     ; ->RPCS1 */

    INT64_C(0201100000001),                 /*20    movei 2,1       ; blk #1 */
    INT64_C(0265740377041),                 /*      jsp 17,rdbl     ; read */
    INT64_C(0204140001000),                 /*      movs 3,1000     ; id word */
    INT64_C(0306140505755),                 /*      cain 3,sixbit /HOM/ */
    INT64_C(0254000377032),                 /*      jrst pg         ; match */
    INT64_C(0201100000010),                 /*      movei 2,10      ; blk #10 */
    INT64_C(0265740377041),                 /*      jsp 17,rdbl     ; read */
    INT64_C(0204140001000),                 /*      movs 3,1000     ; id word */

    INT64_C(0302140505755),                 /*30    caie 3,sixbit /HOM/ */
    INT64_C(0254000377061),                 /*      jrst alt2        ; inv home */
    INT64_C(0336100001103),                 /* pg:  skipn 2,1103    ; pg of ptrs */
    INT64_C(0254200377033),                 /*      halt .          ; inv ptr */
    INT64_C(0265740377041),                 /*      jsp 17,rdbl     ; read */
    INT64_C(0336100001004),                 /*      skipn 2,1004    ; mon boot */
    INT64_C(0254200377036),                 /*      halt .          ; inv ptr */
    INT64_C(0265740377041),                 /*      jsp 17,rdbl     ; read */

    INT64_C(0254000001000),                 /*40    jrst 1000       ; start */
    INT64_C(0201140176000),                 /* rdbl:movei 3,176000  ; wd cnt 1P = -512*2 */
    INT64_C(0201200004000),                 /*      movei 4,4000    ; 11 addr => M[1000] */
    INT64_C(0200300000002),                 /*      move 6,2 */
    INT64_C(0242300777750),                 /*      lsh 6,-24.      ; cyl */
    INT64_C(0713141000002),                 /*      wrio 3,2(1)     ; ->RPWC */
    INT64_C(0713201000004),                 /*      wrio 4,4(1)     ; ->RPBA */
    INT64_C(0713101000006),                 /*      wrio 2,6(1)     ; ->RPDA */

    INT64_C(0713301000034),                 /*50    wrio 6,34(1)    ; ->RPDC */
    INT64_C(0201000000071),                 /*      movei 0,71      ; read+go */
    INT64_C(0713001000000),                 /*      wrio 0,0(1)     ; ->RPCS1 */
    INT64_C(0712341000000),                 /*      rdio 7,0(1)     ; read csr */
    INT64_C(0606340000200),                 /*      trnn 7,200      ; test rdy */
    INT64_C(0254000377053),                 /*      jrst .-2        ; loop */
    INT64_C(0602340100000),                 /*      trne 7,100000   ; test err */
    INT64_C(0254200377057),                 /*      halt . */

    INT64_C(0254017000000),                 /*60    jrst 0(17)      ; return */
    INT64_C(0201100000012),                 /*alt2: movei 2,10.     ; blk #10. */
    INT64_C(0265740377041),                 /*      jsp 17,rdbl     ; read */
    INT64_C(0204140001000),                 /*      movs 3,1000     ; id word */
    INT64_C(0302140505755),                 /*      caie 3,sixbit /HOM/ */
    INT64_C(0254200377065),                 /*      halt .          ; inv home */
    INT64_C(0254000377032),                 /*      jrst pg         ; Read ptrs */
    };

static const d10 boot_rom_its[] = {
    INT64_C(0510040000001)+FE_RHBASE,       /* boot:hllzi 1,FE_RHBASE ; uba # */
    INT64_C(0201000140001),                 /*      movei 0,140001  ; vld,fst,pg 1 */
    INT64_C(0715000000000)+((IOBA_UBMAP+1) & RMASK),   /*      iowrq 0,763001  ; set ubmap */
    INT64_C(0200040000000)+FE_RHBASE,       /*      move 1,FE_RHBASE */
    INT64_C(0201000000040),                 /*      movei 0,40      ; ctrl reset */
    INT64_C(0715001000010),                 /*      iowrq 0,10(1)   ; ->RPCS2 */
    INT64_C(0200240000000)+FE_UNIT,         /*      move 5,FE_UNIT  ; unit */
    INT64_C(0715241000010),                 /*      iowrq 5,10(1)   ; ->RPCS2 */

    INT64_C(0711001000012),                 /*10    iordq 0,12(1)   ; RPDS */
    INT64_C(0640000010600),                 /*      trc  0,10600    ; MOL + DPR + RDY */
    INT64_C(0642000010600),                 /*      trce 0,10600    ; */
    INT64_C(0254000377010),                 /*      jrst .-3        ; wait */
    INT64_C(0201000000377),                 /*      movei 0,377     ; All units */
    INT64_C(0715001000016),                 /*      iowrq 0,16(1)   ; Clear on-line attns */
    INT64_C(0201000000021),                 /*      movei 0,21      ; preset */
    INT64_C(0715001000000),                 /*      iowrq 0,0(1)    ; ->RPCS1 */

    INT64_C(0201100000001),                 /*20    movei 2,1       ; blk #1 */
    INT64_C(0265740377041),                 /*      jsp 17,rdbl     ; read */
    INT64_C(0204140001000),                 /*      movs 3,1000     ; id word */
    INT64_C(0306140505755),                 /*      cain 3,sixbit /HOM/ */
    INT64_C(0254000377032),                 /*      jrst pg         ; match */
    INT64_C(0201100000010),                 /*      movei 2,10      ; blk #10 */
    INT64_C(0265740377041),                 /*      jsp 17,rdbl     ; read */
    INT64_C(0204140001000),                 /*      movs 3,1000     ; id word */

    INT64_C(0302140505755),                 /*30    caie 3,sixbit /HOM/ */
    INT64_C(0254000377061),                 /*      jrst alt2       ; inv home */
    INT64_C(0336100001103),                 /* pg:  skipn 2,1103    ; pg of ptrs */
    INT64_C(0254200377033),                 /*      halt .          ; inv ptr */
    INT64_C(0265740377041),                 /*      jsp 17,rdbl     ; read */
    INT64_C(0336100001004),                 /*      skipn 2,1004    ; mon boot */
    INT64_C(0254200377036),                 /*      halt .          ; inv ptr */
    INT64_C(0265740377041),                 /*      jsp 17,rdbl     ; read */

    INT64_C(0254000001000),                 /*40    jrst 1000       ; start */
    INT64_C(0201140176000),                 /* rdbl:movei 3,176000  ; wd cnt 1P = -512 *2 */
    INT64_C(0201200004000),                 /*      movei 4,4000    ; addr */
    INT64_C(0200300000002),                 /*      move 6,2 */
    INT64_C(0242300777750),                 /*      lsh 6,-24.      ; cyl */
    INT64_C(0715141000002),                 /*      iowrq 3,2(1)    ; ->RPWC */
    INT64_C(0715201000004),                 /*      iowrq 4,4(1)    ; ->RPBA */
    INT64_C(0715101000006),                 /*      iowrq 2,6(1)    ; ->RPDA */

    INT64_C(0715301000034),                 /*50    iowrq 6,34(1)   ; ->RPDC */
    INT64_C(0201000000071),                 /*      movei 0,71      ; read+go */
    INT64_C(0715001000000),                 /*      iowrq 0,0(1)    ; ->RPCS1 */
    INT64_C(0711341000000),                 /*      iordq 7,0(1)    ; read csr */
    INT64_C(0606340000200),                 /*      trnn 7,200      ; test rdy */
    INT64_C(0254000377053),                 /*      jrst .-2        ; loop */
    INT64_C(0602340100000),                 /*      trne 7,100000   ; test err */
    INT64_C(0254200377057),                 /*      halt */

    INT64_C(0254017000000),                 /*60    jrst 0(17)      ; return */
    INT64_C(0201100000012),                 /* alt2:movei 2,10.     ; blk #10. */
    INT64_C(0265740377041),                 /*      jsp 17,rdbl     ; read */
    INT64_C(0204140001000),                 /*      movs 3,1000     ; id word */
    INT64_C(0302140505755),                 /*      caie 3,sixbit /HOM/ */
    INT64_C(0254200377065),                 /*      halt .          ; inv home */
    INT64_C(0254000377032),                 /*      jrst pg         ; Read ptrs */
    };

t_stat rp_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern a10 saved_PC;
UNIT *uptr;

unitno &= CS2_M_UNIT;
uptr = rp_dev.units + unitno;
if (!(uptr->flags & UNIT_ATT))
    return SCPE_NOATT;

M[FE_RHBASE] = fe_bootrh = rp_dib.ba;
M[FE_UNIT] = fe_bootunit = unitno;

ASSURE (sizeof(boot_rom_dec) == sizeof(boot_rom_its));

M[FE_KEEPA] = (M[FE_KEEPA] & ~INT64_C(0xFF)) | ((sim_switches & SWMASK ('A'))? 010 : 0);

for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = Q_ITS? boot_rom_its[i]: boot_rom_dec[i];
saved_PC = BOOT_START;
return SCPE_OK;
}

t_stat rp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  fprintf (st, "RP04/05/06/07, RM02/03/05/80 Disk Pack Drives (RP)\n\n");
  fprintf (st, "The RP controller implements the Massbus family of large disk dri\
ves.  RP\n");
  fprintf (st, "options include the ability to set units write enabled or write locked, to\n");
  fprintf (st, "set the drive type to one of six disk types or autosize, and to write a DEC\n");
  fprintf (st, "standard 044 compliant bad block table on the last track.\n\n");
  fprint_set_help (st, dptr);
  fprint_show_help (st, dptr);
  fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
  fprintf (st, "The bad block option can be used only when a unit is attached to a file.\n");
  fprintf (st, "The RP device supports the BOOT command.\n");
  fprint_reg_help (st, dptr);
  fprintf (st, "\nError handling is as follows:\n\n");
  fprintf (st, "    error         STOP_IOE   processed as\n");
  fprintf (st, "    not attached  1          report error and stop\n");
  fprintf (st, "                  0          disk not ready\n\n");
  fprintf (st, "    end of file   x          assume rest of disk is zero\n");
  fprintf (st, "    OS I/O error  x          report error and stop\n");
  fprintf (st, "\nDisk drives on the %s device can be attacbed to simulated storage in the\n", dptr->name);
  fprintf (st, "following ways:\n\n");
  sim_disk_attach_help (st, dptr, uptr, flag, cptr);
  return SCPE_OK;
}

const char *rp_description (DEVICE *dptr)
{
  return "RP04/05/06/07 RM02/03/05/80 Massbus disk controller";
}
