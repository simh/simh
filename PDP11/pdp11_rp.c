/* pdp11_rp.c - RP04/05/06/07 RM02/03/05/80 Massbus disk controller

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

   rp           RH/RP/RM moving head disks

   17-May-07    RMS     CS1 DVA resides in device, not MBA
   21-Nov-05    RMS     Enable/disable device also enables/disables Massbus adapter
   12-Nov-05	RMS     Fixed DriveClear, does not clear disk address
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   18-Mar-05    RMS     Added attached test to detach routine
   12-Sep-04    RMS     Cloned from pdp11_rp.c

   Note: The VMS driver and the RP controller documentation state that

        ER2 =   offset 8
        SN =    offset 12

   But the TOPS-10 and TOPS-20 drivers, and the RP schematics state that

        SN =    offset 8
        ER2 =   offset 12

   The simulation follows the schematics.  The VMS drivers defines but does
   not use these offsets, and the error logger follows the schematics.
*/

#if defined (VM_PDP10)
#error "PDP-10 uses pdp10_rp.c!"

#elif defined (VM_PDP11)
#include "pdp11_defs.h"
#define INIT_DTYPE      RM03_DTYPE
#define INIT_SIZE       RM03_SIZE

#elif defined (VM_VAX)
#include "vax_defs.h"
#define INIT_DTYPE      RP06_DTYPE
#define INIT_SIZE       RP06_SIZE
#define DMASK           0xFFFF
#if (!UNIBUS)
#error "Qbus not supported!"
#endif

#endif

#include <math.h>

#define RP_CTRL         0                               /* ctrl is RP */
#define RM_CTRL         1                               /* ctrl is RM */
#define RP_NUMDR        8                               /* #drives */
#define RP_NUMWD        256                             /* words/sector */
#define RP_MAXFR        (1 << 16)                       /* max transfer */
#define GET_SECTOR(x,d) ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) drv_tab[d].sect)))
#define RM_OF           (MBA_RMASK + 1)

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    7
#define UNIT_V_AUTO     (UNIT_V_UF + 4)                 /* autosize */
#define UNIT_V_DUMMY    (UNIT_V_UF + 5)                 /* dummy flag */
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_DUMMY      (1 << UNIT_V_DUMMY)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write prot */

/* Parameters in the unit descriptor */

#define CYL             u3                              /* current cylinder */

/* RPCS1, RMCS1 - control/status 1 - offset 0 */

#define RP_CS1_OF       0
#define RM_CS1_OF       (0 + RM_OF)
#define CS1_GO          CSR_GO                          /* go */
#define CS1_V_FNC       1                               /* function pos */
#define CS1_M_FNC       037                             /* function mask */
#define CS1_N_FNC       (CS1_M_FNC + 1)
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
#define CS1_RW          076
#define CS1_DVA         04000                           /* drive avail */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)

/* RPDS, RMDS - drive status - offset 1 */

#define RP_DS_OF        1
#define RM_DS_OF        (1 + RM_OF)
#define DS_OFM          0000001                         /* offset mode */
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

/* RPER1, RMER1 - error status 1 - offset 2 */

#define RP_ER1_OF       2
#define RM_ER1_OF       (2 + RM_OF)
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

/* RPMR, RMMR - maintenace register - offset 3*/

#define RP_MR_OF        3
#define RM_MR_OF        (3 + RM_OF)

/* RPAS, RMAS - attention summary - offset 4 */

#define RP_AS_OF        4
#define RM_AS_OF        (4 + RM_OF)
#define AS_U0           0000001                         /* unit 0 flag */

/* RPDA, RMDA - sector/track - offset 5 */

#define RP_DA_OF        5
#define RM_DA_OF        (5 + RM_OF)
#define DA_V_SC         0                               /* sector pos */
#define DA_M_SC         077                             /* sector mask */
#define DA_V_SF         8                               /* track pos */
#define DA_M_SF         077                             /* track mask */
#define DA_MBZ          0140300
#define GET_SC(x)       (((x) >> DA_V_SC) & DA_M_SC)
#define GET_SF(x)       (((x) >> DA_V_SF) & DA_M_SF)

/* RPDT, RMDT - drive type - offset 6 */

#define RP_DT_OF        6
#define RM_DT_OF        (6 + RM_OF)

/* RPLA, RMLA - look ahead register - offset 7 */

#define RP_LA_OF        7
#define RM_LA_OF        (7 + RM_OF)
#define LA_V_SC         6                               /* sector pos */

/* RPSN, RMSN - serial number - offset 8 */

#define RP_SN_OF        8
#define RM_SN_OF        (8 + RM_OF)

/* RPOF, RMOF  - offset register - offset 9 */

#define RP_OF_OF        9
#define RM_OF_OF        (9 + RM_OF)
#define OF_HCI          0002000                         /* hdr cmp inh NI */
#define OF_ECI          0004000                         /* ECC inhibit NI */
#define OF_F22          0010000                         /* format NI */
#define OF_MBZ          0161400

/* RPDC, RMDC - desired cylinder - offset 10 */

#define RP_DC_OF        10
#define RM_DC_OF        (10 + RM_OF)
#define DC_V_CY         0                               /* cylinder pos */
#define DC_M_CY         01777                           /* cylinder mask */
#define DC_MBZ          0176000
#define GET_CY(x)       (((x) >> DC_V_CY) & DC_M_CY)
#define GET_DA(c,fs,d)  ((((GET_CY (c) * drv_tab[d].surf) + \
                        GET_SF (fs)) * drv_tab[d].sect) + GET_SC (fs))

/* RPCC - current cylinder - offset 11
   RMHR - holding register - offset 11 */

#define RP_CC_OF        11
#define RM_HR_OF        (11 + RM_OF)

/* RPER2 - error status 2 - drive unsafe conditions - unimplemented - offset 12
   RMMR2 - maintenance register - unimplemented - offset 12 */

#define RP_ER2_OF       12
#define RM_MR2_OF       (12 + RM_OF)

/* RPER3 - error status 3 - more unsafe conditions - unimplemented - offset 13
   RMER2 - error status 2 - unimplemented - offset 13 */

#define RP_ER3_OF       13
#define RM_ER2_OF       (13 + RM_OF)

/* RPEC1, RMEC1 - ECC status 1 - unimplemented - offset 14 */

#define RP_EC1_OF       14
#define RM_EC1_OF       (14 + RM_OF)

/* RPEC2, RMEC1 - ECC status 2 - unimplemented - offset 15 */

#define RP_EC2_OF       15
#define RM_EC2_OF       (15 + RM_OF)

/* This controller supports many different disk drive types:

   type         #sectors/       #surfaces/      #cylinders/
                 surface         cylinder        drive

   RM02/3       32              5               823             =67MB
   RP04/5       22              19              411             =88MB
   RM80         31              14              559             =124MB
   RP06         22              19              815             =176MB
   RM05         32              19              823             =256MB
   RP07         50              32              630             =516MB

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE.

   Note: the RP07, despite its designation, belongs to the RM family
*/

#define RM03_DTYPE      0
#define RM03_SECT       32
#define RM03_SURF       5
#define RM03_CYL        823
#define RM03_DEV        020024
#define RM03_SIZE       (RM03_SECT * RM03_SURF * RM03_CYL * RP_NUMWD)

#define RP04_DTYPE      1
#define RP04_SECT       22
#define RP04_SURF       19
#define RP04_CYL        411
#define RP04_DEV        020020
#define RP04_SIZE       (RP04_SECT * RP04_SURF * RP04_CYL * RP_NUMWD)

#define RM80_DTYPE      2
#define RM80_SECT       31
#define RM80_SURF       14
#define RM80_CYL        559
#define RM80_DEV        020026
#define RM80_SIZE       (RM80_SECT * RM80_SURF * RM80_CYL * RP_NUMWD)

#define RP06_DTYPE      3
#define RP06_SECT       22
#define RP06_SURF       19
#define RP06_CYL        815
#define RP06_DEV        020022
#define RP06_SIZE       (RP06_SECT * RP06_SURF * RP06_CYL * RP_NUMWD)

#define RM05_DTYPE      4
#define RM05_SECT       32
#define RM05_SURF       19
#define RM05_CYL        823
#define RM05_DEV        020027
#define RM05_SIZE       (RM05_SECT * RM05_SURF * RM05_CYL * RP_NUMWD)

#define RP07_DTYPE      5
#define RP07_SECT       50
#define RP07_SURF       32
#define RP07_CYL        630
#define RP07_DEV        020042
#define RP07_SIZE       (RP07_SECT * RP07_SURF * RP07_CYL * RP_NUMWD)

#define RP_CTRL         0
#define RM_CTRL         1

struct drvtyp {
    int32       sect;                                   /* sectors */
    int32       surf;                                   /* surfaces */
    int32       cyl;                                    /* cylinders */
    int32       size;                                   /* #blocks */
    int32       devtype;                                /* device type */
    int32       ctrl;                                   /* ctrl type */
    };

static struct drvtyp drv_tab[] = {
    { RM03_SECT, RM03_SURF, RM03_CYL, RM03_SIZE, RM03_DEV, RM_CTRL },
    { RP04_SECT, RP04_SURF, RP04_CYL, RP04_SIZE, RP04_DEV, RP_CTRL },
    { RM80_SECT, RM80_SURF, RM80_CYL, RM80_SIZE, RM80_DEV, RM_CTRL },
    { RP06_SECT, RP06_SURF, RP06_CYL, RP06_SIZE, RP06_DEV, RP_CTRL },
    { RM05_SECT, RM05_SURF, RM05_CYL, RM05_SIZE, RM05_DEV, RM_CTRL },
    { RP07_SECT, RP07_SURF, RP07_CYL, RP07_SIZE, RP07_DEV, RM_CTRL },
    { 0 }
    };

uint16 *rpxb = NULL;                                    /* xfer buffer */
uint16 rpcs1[RP_NUMDR] = { 0 };                         /* control/status 1 */
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
int32 rp_stopioe = 1;                                   /* stop on error */
int32 rp_swait = 10;                                    /* seek time */
int32 rp_rwait = 10;                                    /* rotate time */
static const char *rp_fname[CS1_N_FNC] = {
    "NOP", "UNLD", "SEEK", "RECAL", "DCLR", "RLS", "OFFS", "RETN",
    "PRESET", "PACK", "12", "13", "SCH", "15", "16", "17",
    "20", "21", "22", "23", "WRCHK", "25", "26", "27",
    "WRITE", "WRHDR", "32", "33", "READ", "RDHDR", "36", "37"
    };

extern FILE *sim_deb;

t_stat rp_mbrd (int32 *data, int32 ofs, int32 drv);
t_stat rp_mbwr (int32 data, int32 ofs, int32 drv);
t_stat rp_svc (UNIT *uptr);
t_stat rp_reset (DEVICE *dptr);
t_stat rp_attach (UNIT *uptr, char *cptr);
t_stat rp_detach (UNIT *uptr);
t_stat rp_boot (int32 unitno, DEVICE *dptr);
void rp_set_er (int32 flg, int32 drv);
void rp_clr_as (int32 mask);
void rp_update_ds (int32 flg, int32 drv);
t_stat rp_go (int32 drv);
t_stat rp_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat rp_set_bad (UNIT *uptr, int32 val, char *cptr, void *desc);
int32 rp_abort (void);

/* RP data structures

   rp_dev       RP device descriptor
   rp_unit      RP unit list
   rp_reg       RP register list
   rp_mod       RP modifier list
*/

DIB rp_dib = { MBA_RP, 0, &rp_mbrd, &rp_mbwr, 0, 0, 0, { &rp_abort } };

UNIT rp_unit[] = {
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(INIT_DTYPE << UNIT_V_DTYPE), INIT_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(INIT_DTYPE << UNIT_V_DTYPE), INIT_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(INIT_DTYPE << UNIT_V_DTYPE), INIT_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(INIT_DTYPE << UNIT_V_DTYPE), INIT_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(INIT_DTYPE << UNIT_V_DTYPE), INIT_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(INIT_DTYPE << UNIT_V_DTYPE), INIT_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(INIT_DTYPE << UNIT_V_DTYPE), INIT_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(INIT_DTYPE << UNIT_V_DTYPE), INIT_SIZE) }
    };

REG rp_reg[] = {
    { BRDATA (CS1, rpcs1, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (DA, rpda, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (DS, rpds, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (ER1, rper1, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (HR, rmhr, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (OF, rpof, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (DC, rpdc, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (ER2, rper2, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (ER3, rper3, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (EC1, rpec1, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (EC2, rpec2, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (MR, rpmr, DEV_RDX, 16, RP_NUMDR) },
    { BRDATA (MR2, rmmr2, DEV_RDX, 16, RP_NUMDR) },
    { DRDATA (STIME, rp_swait, 24), REG_NZ + PV_LEFT },
    { DRDATA (RTIME, rp_rwait, 24), REG_NZ + PV_LEFT },
    { URDATA (CAPAC, rp_unit[0].capac, 10, T_ADDR_W, 0,
              RP_NUMDR, PV_LEFT | REG_HRO) },
    { FLDATA (STOP_IOE, rp_stopioe, 0) },
    { GRDATA (CTRLTYPE, rp_dib.lnt, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

MTAB rp_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "MASSBUS", "MASSBUS", NULL, &mba_show_num },
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { UNIT_DUMMY, 0, NULL, "BADBLOCK", &rp_set_bad },
    { (UNIT_DTYPE+UNIT_ATT), (RM03_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "RM03", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (RP04_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "RP04", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (RM80_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "RM80", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (RP06_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "RP06", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (RM05_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "RM05", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (RP07_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "RP07", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RM03_DTYPE << UNIT_V_DTYPE),
      "RM03", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RP04_DTYPE << UNIT_V_DTYPE),
      "RP04", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RM80_DTYPE << UNIT_V_DTYPE),
      "RM80", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RP06_DTYPE << UNIT_V_DTYPE),
      "RP06", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RM05_DTYPE << UNIT_V_DTYPE),
      "RM05", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RP07_DTYPE << UNIT_V_DTYPE),
      "RP07", NULL, NULL },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
    { UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL },
    { (UNIT_AUTO+UNIT_DTYPE), (RM03_DTYPE << UNIT_V_DTYPE),
      NULL, "RM03", &rp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (RP04_DTYPE << UNIT_V_DTYPE),
      NULL, "RP04", &rp_set_size }, 
    { (UNIT_AUTO+UNIT_DTYPE), (RM80_DTYPE << UNIT_V_DTYPE),
      NULL, "RM80", &rp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (RP06_DTYPE << UNIT_V_DTYPE),
      NULL, "RP06", &rp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (RM05_DTYPE << UNIT_V_DTYPE),
      NULL, "RM05", &rp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (RP07_DTYPE << UNIT_V_DTYPE),
      NULL, "RP07", &rp_set_size },
    { 0 }
    };

DEVICE rp_dev = {
    "RP", rp_unit, rp_reg, rp_mod,
    RP_NUMDR, DEV_RDX, 30, 1, DEV_RDX, 16,
    NULL, NULL, &rp_reset,
    &rp_boot, &rp_attach, &rp_detach,
    &rp_dib, DEV_DISABLE | DEV_UBUS | DEV_QBUS | DEV_MBUS | DEV_DEBUG
    };

/* Massbus register read */

t_stat rp_mbrd (int32 *data, int32 ofs, int32 drv)
{
uint32 val, dtype, i;
UNIT *uptr;

rp_update_ds (0, drv);                                  /* update ds */
uptr = rp_dev.units + drv;                              /* get unit */
if (uptr->flags & UNIT_DIS) {                           /* nx disk */
    *data = 0;
    return MBE_NXD;
    }
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
ofs = ofs & MBA_RMASK;                                  /* mask offset */
if (drv_tab[dtype].ctrl == RM_CTRL)                     /* RM? convert */
    ofs = ofs + RM_OF;

switch (ofs) {                                          /* decode offset */

    case RP_CS1_OF: case RM_CS1_OF:                     /* RPCS1 */
        val = (rpcs1[drv] & CS1_RW) | CS1_DVA;          /* DVA always set */
        break;

    case RP_DA_OF: case RM_DA_OF:                       /* RPDA */
        val = rpda[drv] = rpda[drv] & ~DA_MBZ;
        break;

    case RP_DS_OF: case RM_DS_OF:                       /* RPDS */
        val = rpds[drv];
        break;

    case RP_ER1_OF: case RM_ER1_OF:                     /* RPER1 */
        val = rper1[drv];
        break;

    case RP_AS_OF: case RM_AS_OF:                       /* RPAS */
        val = 0;
        for (i = 0; i < RP_NUMDR; i++) {
            if (rpds[i] & DS_ATA)
                val |= (AS_U0 << i);
            }
        break;

    case RP_LA_OF: case RM_LA_OF:                       /* RPLA */
        val = GET_SECTOR (rp_rwait, dtype) << LA_V_SC;
        break;

    case RP_MR_OF: case RM_MR_OF:                       /* RPMR */
        val = rpmr[drv];
        break;

    case RP_DT_OF: case RM_DT_OF:                       /* RPDT */
        val = drv_tab[dtype].devtype;
        break;

    case RP_SN_OF: case RM_SN_OF:                       /* RPSN */
        val = 020 | (drv + 1);
        break;

    case RP_OF_OF: case RM_OF_OF:                       /* RPOF */
        val = rpof[drv] = rpof[drv] & ~OF_MBZ;
        break;

    case RP_DC_OF: case RM_DC_OF:                       /* RPDC */
        val = rpdc[drv] = rpdc[drv] & ~DC_MBZ;
        break;

    case RP_CC_OF:                                      /* RPCC */
        val = rp_unit[drv].CYL;
        break;

    case RP_ER2_OF: case RM_ER2_OF:                     /* RPER2 */
        val = rper2[drv];
        break;

    case RP_ER3_OF:                                     /* RPER3 */
        val = rper3[drv];
        break;

    case RP_EC1_OF: case RM_EC1_OF:                     /* RPEC1 */
        val = rpec1[drv];
        break;

    case RP_EC2_OF: case RM_EC2_OF:                     /* RPEC2 */
        val = rpec2[drv];
        break;

    case RM_HR_OF:                                      /* RMHR */
        val = rmhr[drv] ^ DMASK;
        break;

    case RM_MR2_OF:                                     /* RHMR2 */
        val = rmmr2[drv];
        break;

   default:                                             /* all others */
        *data = 0;
        return MBE_NXR;
        }

*data = val;
return SCPE_OK;
}

/* Massbus register write */

t_stat rp_mbwr (int32 data, int32 ofs, int32 drv)
{
int32 dtype;
UNIT *uptr;

uptr = rp_dev.units + drv;                              /* get unit */
if (uptr->flags & UNIT_DIS)                             /* nx disk */
    return MBE_NXD;
if ((ofs != RP_AS_OF) && sim_is_active (uptr)) {        /* unit busy? */
    rp_set_er (ER1_RMR, drv);                           /* won't write */
    rp_update_ds (0, drv);
    return SCPE_OK;
    }
rmhr[drv] = data;                                       /* save write */
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
ofs = ofs & MBA_RMASK;                                  /* mask offset */
if (drv_tab[dtype].ctrl == RM_CTRL)                     /* RM? convert */
    ofs = ofs + RM_OF;

switch (ofs) {                                          /* decode PA<5:1> */

    case RP_CS1_OF: case RM_CS1_OF:                     /* RPCS1 */
        rpcs1[drv] = data & CS1_RW;
        if (data & CS1_GO)                              /* start op */
            return rp_go (drv);
        break;  

    case RP_DA_OF: case RM_DA_OF:                       /* RPDA */
        rpda[drv] = data & ~DA_MBZ;
        break;

    case RP_AS_OF: case RM_AS_OF:                       /* RPAS */
        rp_clr_as (data);
        break;

    case RP_MR_OF: case RM_MR_OF:                       /* RPMR */
        rpmr[drv] = data;
        break;

    case RP_OF_OF: case RM_OF_OF:                       /* RPOF */
        rpof[drv] = data & ~OF_MBZ;
        break;

    case RP_DC_OF: case RM_DC_OF:                       /* RPDC */
        rpdc[drv] = data & ~DC_MBZ;
        break;

    case RM_MR2_OF:                                     /* RMMR2 */
        rmmr2[drv] = data;
        break;

    case RP_ER1_OF: case RM_ER1_OF:                     /* RPER1 */
    case RP_DS_OF: case RM_DS_OF:                       /* RPDS */
    case RP_LA_OF: case RM_LA_OF:                       /* RPLA */
    case RP_DT_OF: case RM_DT_OF:                       /* RPDT */
    case RP_SN_OF: case RM_SN_OF:                       /* RPSN */
    case RP_CC_OF:                                      /* RPCC */
    case RP_ER2_OF: case RM_ER2_OF:                     /* RPER2 */
    case RP_ER3_OF:                                     /* RPER3 */
    case RP_EC1_OF: case RM_EC1_OF:                     /* RPEC1 */
    case RP_EC2_OF: case RM_EC2_OF:                     /* RPEC2 */
    case RM_HR_OF:                                      /* RMHR */
        break;                                          /* read only */

    default:                                            /* all others */
        return MBE_NXR;
        }                                               /* end switch */

rp_update_ds (0, drv);                                  /* update status */
return SCPE_OK;
}

/* Initiate operation - unit not busy, function set */

t_stat rp_go (int32 drv)
{
int32 dc, fnc, dtype, t;
UNIT *uptr;

fnc = GET_FNC (rpcs1[drv]);                             /* get function */
if (DEBUG_PRS (rp_dev))
    fprintf (sim_deb, ">>RP%d STRT: fnc=%s, ds=%o, cyl=%o, da=%o, er=%o\n",
             drv, rp_fname[fnc], rpds[drv], rpdc[drv], rpda[drv], rper1[drv]);
uptr = rp_dev.units + drv;                              /* get unit */
rp_clr_as (AS_U0 << drv);                               /* clear attention */
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
dc = rpdc[drv];                                         /* assume seek, sch */
if ((fnc != FNC_DCLR) && (rpds[drv] & DS_ERR)) {        /* err & ~clear? */
    rp_set_er (ER1_ILF, drv);                           /* not allowed */
    rp_update_ds (DS_ATA, drv);                         /* set attention */
    return MBE_GOE;
    }

switch (fnc) {                                          /* case on function */

    case FNC_DCLR:                                      /* drive clear */
        rper1[drv] = rper2[drv] = rper3[drv] = 0;       /* clear errors */
        rpec2[drv] = 0;                                 /* clear EC2 */
        if (drv_tab[dtype].ctrl == RM_CTRL)             /* RM? */
            rpmr[drv] = 0;                              /* clear maint */
        else rpec1[drv] = 0;                            /* RP, clear EC1 */
    case FNC_NOP:                                       /* no operation */
    case FNC_RELEASE:                                   /* port release */
        return SCPE_OK;

    case FNC_PRESET:                                    /* read-in preset */
        rpdc[drv] = 0;                                  /* clear disk addr */
        rpda[drv] = 0;
        rpof[drv] = 0;                                  /* clear offset */
    case FNC_PACK:                                      /* pack acknowledge */
        rpds[drv] = rpds[drv] | DS_VV;                  /* set volume valid */
        return SCPE_OK;

    case FNC_OFFSET:                                    /* offset mode */
    case FNC_RETURN:
        if ((uptr->flags & UNIT_ATT) == 0) {            /* not attached? */
            rp_set_er (ER1_UNS, drv);                   /* unsafe */
            break;
            }
        rpds[drv] = (rpds[drv] & ~DS_RDY) | DS_PIP;     /* set positioning */
        sim_activate (uptr, rp_swait);                  /* time operation */
        return SCPE_OK;

    case FNC_UNLOAD:                                    /* unload */
    case FNC_RECAL:                                     /* recalibrate */
        dc = 0;                                         /* seek to 0 */
    case FNC_SEEK:                                      /* seek */
    case FNC_SEARCH:                                    /* search */
        if ((uptr->flags & UNIT_ATT) == 0) {            /* not attached? */
            rp_set_er (ER1_UNS, drv);                   /* unsafe */
            break;
            }
        if ((GET_CY (dc) >= drv_tab[dtype].cyl) ||      /* bad cylinder */
            (GET_SF (rpda[drv]) >= drv_tab[dtype].surf) || /* bad surface */
            (GET_SC (rpda[drv]) >= drv_tab[dtype].sect)) { /* or bad sector? */
            rp_set_er (ER1_IAE, drv);
            break;
            }
        rpds[drv] = (rpds[drv] & ~DS_RDY) | DS_PIP;     /* set positioning */
        t = abs (dc - uptr->CYL);                       /* cyl diff */
        if (t == 0)                                     /* min time */
            t = 1;
        sim_activate (uptr, rp_swait * t);              /* schedule */
        uptr->CYL = dc;                                 /* save cylinder */
        return SCPE_OK;

    case FNC_WRITEH:                                    /* write headers */
    case FNC_WRITE:                                     /* write */
    case FNC_WCHK:                                      /* write check */
    case FNC_READ:                                      /* read */
    case FNC_READH:                                     /* read headers */
        if ((uptr->flags & UNIT_ATT) == 0) {            /* not attached? */
            rp_set_er (ER1_UNS, drv);                   /* unsafe */
            break;
            }
        if ((GET_CY (dc) >= drv_tab[dtype].cyl) ||      /* bad cylinder */
            (GET_SF (rpda[drv]) >= drv_tab[dtype].surf) || /* bad surface */
            (GET_SC (rpda[drv]) >= drv_tab[dtype].sect)) { /* or bad sector? */
            rp_set_er (ER1_IAE, drv);
            break;
            }
        rpds[drv] = rpds[drv] & ~DS_RDY;                /* clear drive rdy */
        sim_activate (uptr, rp_rwait + (rp_swait * abs (dc - uptr->CYL)));
        uptr->CYL = dc;                                 /* save cylinder */
        return SCPE_OK;

    default:                                            /* all others */
        rp_set_er (ER1_ILF, drv);                       /* not supported */
        break;
        }

rp_update_ds (DS_ATA, drv);                             /* set attn, req int */
return MBE_GOE;
}

/* Abort opertion - there is a data transfer in progress */

int32 rp_abort (void)
{
return rp_reset (&rp_dev);
}

/* Service unit timeout

   Complete movement or data transfer command
   Unit must exist - can't remove an active unit
   Unit must be attached - detach cancels in progress operations
*/

t_stat rp_svc (UNIT *uptr)
{
int32 i, fnc, dtype, drv, err;
int32 wc, abc, awc, mbc, da;

dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
drv = (int32) (uptr - rp_dev.units);                    /* get drv number */
da = GET_DA (rpdc[drv], rpda[drv], dtype) * RP_NUMWD;   /* get disk addr */
fnc = GET_FNC (rpcs1[drv]);                             /* get function */

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    rp_set_er (ER1_UNS, drv);                           /* set drive error */
    if (fnc >= FNC_XFER)                                /* xfr? set done */
        mba_set_don (rp_dib.ba);
    rp_update_ds (DS_ATA, drv);                         /* set attn */
    return (rp_stopioe? SCPE_UNATT: SCPE_OK);
    }
rpds[drv] = (rpds[drv] & ~DS_PIP) | DS_RDY;             /* change drive status */

switch (fnc) {                                          /* case on function */

    case FNC_OFFSET:                                    /* offset */
        rp_update_ds (DS_OFM | DS_ATA, drv);
        break;

    case FNC_RETURN:                                    /* return to centerline */
        rpds[drv] = rpds[drv] & ~DS_OFM;                /* clear offset, set attn */
        rp_update_ds (DS_ATA, drv);
        break;  

    case FNC_UNLOAD:                                    /* unload */
        rp_detach (uptr);                               /* detach unit */
        break;

    case FNC_RECAL:                                     /* recalibrate */
    case FNC_SEARCH:                                    /* search */
    case FNC_SEEK:                                      /* seek */
        rp_update_ds (DS_ATA, drv);
        break;

    case FNC_WRITE:                                     /* write */
        if (uptr->flags & UNIT_WPRT) {                  /* write locked? */
            rp_set_er (ER1_WLE, drv);                   /* set drive error */
            mba_set_exc (rp_dib.ba);                    /* set exception */
            rp_update_ds (DS_ATA, drv);                 /* set attn */
            return SCPE_OK;
            }
    case FNC_WCHK:                                      /* write check */
    case FNC_READ:                                      /* read */
    case FNC_READH:                                     /* read headers */
        err = fseek (uptr->fileref, da * sizeof (int16), SEEK_SET);
        mbc = mba_get_bc (rp_dib.ba);                   /* get byte count */
        wc = (mbc + 1) >> 1;                            /* convert to words */
        if ((da + wc) > drv_tab[dtype].size) {          /* disk overrun? */
            rp_set_er (ER1_AOE, drv);                   /* set err */
            wc = drv_tab[dtype].size - da;              /* trim xfer */
            mbc = wc << 1;                              /* trim mb count */
            if (da >= drv_tab[dtype].size) {            /* none left? */
                mba_set_exc (rp_dib.ba);                /* set exception */
                rp_update_ds (DS_ATA, drv);             /* set attn */
                break;
                }
            }
        if (fnc == FNC_WRITE) {                         /* write? */
            abc = mba_rdbufW (rp_dib.ba, mbc, rpxb);    /* get buffer */
            wc = (abc + 1) >> 1;                        /* actual # wds */
            awc = (wc + (RP_NUMWD - 1)) & ~(RP_NUMWD - 1);
            for (i = wc; i < awc; i++)                  /* fill buf */
                rpxb[i] = 0;
            if (wc && !err) {                           /* write buf */
                fxwrite (rpxb, sizeof (uint16), awc, uptr->fileref);
                err = ferror (uptr->fileref);
                }
            }                                           /* end if wr */
        else {                                          /* read or wchk */
            awc = fxread (rpxb, sizeof (uint16), wc, uptr->fileref);
            err = ferror (uptr->fileref);
            for (i = awc; i < wc; i++)                  /* fill buf */
                rpxb[i] = 0;
            if (fnc == FNC_WCHK)                        /* write check? */
                mba_chbufW (rp_dib.ba, mbc, rpxb);      /* check vs mem */
            else mba_wrbufW (rp_dib.ba, mbc, rpxb);     /* store in mem */
            }                                           /* end if read */
        da = da + wc + (RP_NUMWD - 1);
        if (da >= drv_tab[dtype].size)
            rpds[drv] = rpds[drv] | DS_LST;
        da = da / RP_NUMWD;
        rpda[drv] = da % drv_tab[dtype].sect;
        da = da / drv_tab[dtype].sect;
        rpda[drv] = rpda[drv] | ((da % drv_tab[dtype].surf) << DA_V_SF);
        rpdc[drv] = da / drv_tab[dtype].surf;
        uptr->CYL = rpdc[drv];

        if (err != 0) {                                 /* error? */
            rp_set_er (ER1_PAR, drv);                   /* set drive error */
            mba_set_exc (rp_dib.ba);                    /* set exception */
            rp_update_ds (DS_ATA, drv);
            perror ("RP I/O error");
            clearerr (uptr->fileref);
            return SCPE_IOERR;
            }

    case FNC_WRITEH:                                    /* write headers stub */
        mba_set_don (rp_dib.ba);                        /* set done */
        rp_update_ds (0, drv);                          /* update ds */
        break;
        }                                               /* end case func */

if (DEBUG_PRS (rp_dev))
    fprintf (sim_deb, ">>RP%d DONE: fnc=%s, ds=%o, cyl=%o, da=%o, er=%d\n",
             drv, rp_fname[fnc], rpds[drv], rpdc[drv], rpda[drv], rper1[drv]);
return SCPE_OK;
}

/* Set drive error */

void rp_set_er (int32 flag, int32 drv)
{
rper1[drv] = rper1[drv] | flag;
rpds[drv] = rpds[drv] | DS_ATA;
mba_upd_ata (rp_dib.ba, 1);
return;
}

/* Clear attention flags */

void rp_clr_as (int32 mask)
{
uint32 i, as;

for (i = as = 0; i < RP_NUMDR; i++) {
    if (mask & (AS_U0 << i))
        rpds[i] &= ~DS_ATA;
    if (rpds[i] & DS_ATA)
        as = 1;
    }
mba_upd_ata (rp_dib.ba, as);
return;
}

/* Drive status update */

void rp_update_ds (int32 flag, int32 drv)
{
if (rp_unit[drv].flags & UNIT_DIS)
    rpds[drv] = rper1[drv] = 0;
else rpds[drv] = (rpds[drv] | DS_DPR) & ~DS_PGM;
if (rp_unit[drv].flags & UNIT_ATT)
    rpds[drv] = rpds[drv] | DS_MOL;
else rpds[drv] = rpds[drv] & ~(DS_MOL | DS_VV | DS_RDY);
if (rper1[drv] | rper2[drv] | rper3[drv])
    rpds[drv] = rpds[drv] | DS_ERR;
else rpds[drv] = rpds[drv] & ~DS_ERR;
rpds[drv] = rpds[drv] | flag;
if (flag & DS_ATA)
    mba_upd_ata (rp_dib.ba, 1);
return;
}

/* Device reset */

t_stat rp_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

mba_set_enbdis (MBA_RP, rp_dev.flags & DEV_DIS);
for (i = 0; i < RP_NUMDR; i++) {
    uptr = rp_dev.units + i;
    sim_cancel (uptr);
    uptr->CYL = 0;
    if (uptr->flags & UNIT_ATT)
        rpds[i] = (rpds[i] & DS_VV) | DS_DPR | DS_RDY | DS_MOL |
                  ((uptr->flags & UNIT_WPRT)? DS_WRL: 0);
    else if (uptr->flags & UNIT_DIS)
        rpds[i] = 0;
    else rpds[i] = DS_DPR;
    rpcs1[i] = 0;
    rper1[i] = 0;
    rpof[i] = 0;
    rpdc[i] = 0;
    rpda[i] = 0;
    rpmr[i] = 0;
    rper2[i] = 0;
    rper3[i] = 0;
    rpec1[i] = 0;
    rpec2[i] = 0;
    rmmr2[i] = 0;
    rmhr[i] = 0;
    }
if (rpxb == NULL)
    rpxb = (uint16 *) calloc (RP_MAXFR, sizeof (uint16));
if (rpxb == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

/* Device attach */

t_stat rp_attach (UNIT *uptr, char *cptr)
{
int32 drv, i, p;
t_stat r;

uptr->capac = drv_tab[GET_DTYPE (uptr->flags)].size;
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
drv = (int32) (uptr - rp_dev.units);                    /* get drv number */
rpds[drv] = DS_MOL | DS_RDY | DS_DPR |                  /* upd drv status */
    ((uptr->flags & UNIT_WPRT)? DS_WRL: 0);
rper1[drv] = 0;
rp_update_ds (DS_ATA, drv);                             /* upd ctlr status */

if ((p = sim_fsize (uptr->fileref)) == 0) {             /* new disk image? */
    if (uptr->flags & UNIT_RO)
        return SCPE_OK;
    return pdp11_bad_block (uptr,
        drv_tab[GET_DTYPE (uptr->flags)].sect, RP_NUMWD);
    }
if ((uptr->flags & UNIT_AUTO) == 0)                     /* autosize? */
    return SCPE_OK;
for (i = 0; drv_tab[i].sect != 0; i++) {
    if (p <= (drv_tab[i].size * (int) sizeof (int16))) {
        uptr->flags = (uptr->flags & ~UNIT_DTYPE) | (i << UNIT_V_DTYPE);
        uptr->capac = drv_tab[i].size;
        return SCPE_OK;
        }
    }
return SCPE_OK;
}

/* Device detach */

t_stat rp_detach (UNIT *uptr)
{
int32 drv;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
drv = (int32) (uptr - rp_dev.units);                    /* get drv number */
rpds[drv] = rpds[drv] & ~(DS_MOL | DS_RDY | DS_WRL | DS_VV | DS_OFM);
rp_update_ds (DS_ATA, drv);                             /* request intr */
return detach_unit (uptr);
}

/* Set size command validation routine */

t_stat rp_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 dtype = GET_DTYPE (val);

if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = drv_tab[dtype].size;
return SCPE_OK;
}

/* Set bad block routine */

t_stat rp_set_bad (UNIT *uptr, int32 val, char *cptr, void *desc)
{
return pdp11_bad_block (uptr, drv_tab[GET_DTYPE (uptr->flags)].sect, RP_NUMWD);
}

/* Boot routine */

#if defined (VM_PDP11)

#define BOOT_START      02000                           /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 014)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (uint16))

static const uint16 boot_rom[] = {
    0042102,                        /* "BD" */
    0012706, BOOT_START,            /* mov #boot_start, sp */
    0012700, 0000000,               /* mov #unit, r0 */
    0012701, 0176700,               /* mov #RPCS1, r1 */
    0012761, 0000040, 0000010,      /* mov #CS2_CLR, 10(r1) ; reset */
    0010061, 0000010,               /* mov r0, 10(r1)       ; set unit */
    0012711, 0000021,               /* mov #RIP+GO, (r1)    ; pack ack */
    0012761, 0010000, 0000032,      /* mov #FMT16B, 32(r1)  ; 16b mode */
    0012761, 0177000, 0000002,      /* mov #-512., 2(r1)    ; set wc */
    0005061, 0000004,               /* clr 4(r1)            ; clr ba */
    0005061, 0000006,               /* clr 6(r1)            ; clr da */
    0005061, 0000034,               /* clr 34(r1)           ; clr cyl */
    0012711, 0000071,               /* mov #READ+GO, (r1)   ; read  */
    0105711,                        /* tstb (r1)            ; wait */
    0100376,                        /* bpl .-2 */
    0005002,                        /* clr R2 */
    0005003,                        /* clr R3 */
    0012704, BOOT_START+020,        /* mov #start+020, r4 */
    0005005,                        /* clr R5 */
    0105011,                        /* clrb (r1) */
    0005007                         /* clr PC */
    };

t_stat rp_boot (int32 unitno, DEVICE *dptr)
{
int32 i;
extern int32 saved_PC;
extern uint16 *M;
UNIT *uptr = rp_dev.units + unitno;

for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & (RP_NUMDR - 1);
M[BOOT_CSR >> 1] = mba_get_csr (rp_dib.ba) & DMASK;
if (drv_tab[GET_DTYPE (uptr->flags)].ctrl == RP_CTRL)
    M[BOOT_START >> 1] = 042102;                        /* "BD" */
else M[BOOT_START >> 1] = 042122;                       /* "RD" */
saved_PC = BOOT_ENTRY;
return SCPE_OK;
}

#else

t_stat rp_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_NOFNC;
}

#endif
