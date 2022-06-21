/* kx10_rp.c: DEC Massbus RP04/5/6

   Copyright (c) 2013-2020, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"
#include "kx10_disk.h"

#ifndef NUM_DEVS_RP
#define NUM_DEVS_RP 0
#endif

#if (NUM_DEVS_RP > 0)
#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

#define RP_NUMWD        128     /* 36bit words/sec */
#define NUM_UNITS_RP    8

/* Flags in the unit flags word */

#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    7
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define DTYPE(x)        (((x) & UNIT_M_DTYPE) << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

/* Parameters in the unit descriptor */
#define RPCS1             00
/* RPCS1 - 00 - control */

#define CS1_GO          1               /* go */
#define CS1_V_FNC       1               /* function pos */
#define CS1_M_FNC       037             /* function mask */
#define CS1_FNC         (CS1_M_FNC << CS1_V_FNC)
#define  FNC_NOP        000             /* no operation */
#define  FNC_UNLOAD     001             /* unload */
#define  FNC_SEEK       002             /* seek */
#define  FNC_RECAL      003             /* recalibrate */
#define  FNC_DCLR       004             /* drive clear */
#define  FNC_RELEASE    005             /* port release */
#define  FNC_OFFSET     006             /* offset */
#define  FNC_RETURN     007             /* return to center */
#define  FNC_PRESET     010             /* read-in preset */
#define  FNC_PACK       011             /* pack acknowledge */
#define  FNC_SEARCH     014             /* search */
#define FNC_XFER        024             /* >=? data xfr */
#define  FNC_WCHK       024             /* write check */
#define  FNC_WCHKH      025             /* write check headers */
#define  FNC_WRITE      030             /* write */
#define  FNC_WRITEH     031             /* write w/ headers */
#define  FNC_READ       034             /* read */
#define  FNC_READH      035             /* read w/ headers */
#define CS1_RDY         0000200         /* Drive ready */
#define CS1_DVA         0004000         /* drive avail NI */
#define CS1_SC          0100000         /* Set if TRE or ATTN */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)

#define RPDS            01
/* RPDS - 01 - drive status */

#define DS_OFF          0000001         /* offset mode */
#define DS_VV           0000100         /* volume valid */
#define DS_DRY          0000200         /* drive ready */
#define DS_DPR          0000400         /* drive present */
#define DS_PGM          0001000         /* programable NI */
#define DS_LST          0002000         /* last sector */
#define DS_WRL          0004000         /* write locked */
#define DS_MOL          0010000         /* medium online */
#define DS_PIP          0020000         /* pos in progress */
#define DS_ERR          0040000         /* error */
#define DS_ATA          0100000         /* attention active */
#define DS_MBZ          0000076

#define RPER1           02
/* RPER1 - 02 - error status 1 */

#define ER1_ILF         0000001         /* illegal func */
#define ER1_ILR         0000002         /* illegal register */
#define ER1_RMR         0000004         /* reg mod refused */
#define ER1_PAR         0000010         /* parity err */
#define ER1_FER         0000020         /* format err NI */
#define ER1_WCF         0000040         /* write clk fail NI */
#define ER1_ECH         0000100         /* ECC hard err NI */
#define ER1_HCE         0000200         /* hdr comp err NI */
#define ER1_HCR         0000400         /* hdr CRC err NI */
#define ER1_AOE         0001000         /* addr ovflo err */
#define ER1_IAE         0002000         /* invalid addr err */
#define ER1_WLE         0004000         /* write lock err */
#define ER1_DTE         0010000         /* drive time err NI */
#define ER1_OPI         0020000         /* op incomplete */
#define ER1_UNS         0040000         /* drive unsafe */
#define ER1_DCK         0100000         /* data check NI */

#define RPMR            03
/* RPMR - 03 - maintenace register */

#define RPAS            04
/* RPAS - 04 - attention summary */

#define AS_U0           0000001         /* unit 0 flag */

#define RPDA              05
/* RPDA - 05 - desired sector */

#define DA_V_SC         0               /* sector pos */
#define DA_M_SC         077             /* sector mask */
#define DA_V_SF         8               /* track pos */
#define DA_M_SF         077             /* track mask */
#define DA_MBZ          0140300
#define GET_SC(x)       (((x) >> DA_V_SC) & DA_M_SC)
#define GET_SF(x)       (((x) >> DA_V_SF) & DA_M_SF)

#define RPDT            06
/* RPDT - 06 - drive type */

#define RPLA            07
/* RPLA - 07 - look ahead register */
#define LA_V_SC         6                               /* sector pos */

#define  RPER2          010
/* RPER2 - 10 - error status 2 - drive unsafe conditions - unimplemented */
#define  RPOF           011
/* RPOF  - 11 - offset register */
#define  RPDC           012
/* RPDC  - 12 - desired cylinder */
#define DC_V_CY         0                               /* cylinder pos */
#define DC_M_CY         01777                           /* cylinder mask */
#define DC_MBZ          0176000
#define GET_CY          (((regs[RPDC]) >> DC_V_CY) & DC_M_CY)
#define GET_DA(d)       ((((GET_CY * rp_drv_tab[d].surf) + GET_SF (regs[RPDA])) \
                                   * rp_drv_tab[d].sect) + GET_SC (regs[RPDA]))
#define CCYL           u4
/* RPCC  - 13 - current cylinder */

#define RPSN            014
/* RPSN  - 14 - serial number */
#define RPER3           015
/* RPER3 - 15 - error status 3 - more unsafe conditions - unimplemented */

#define OF_HCI          0002000                         /* hdr cmp inh NI */
#define OF_ECI          0004000                         /* ECC inhibit NI */
#define OF_F22          0010000                         /* format NI */
#define OF_MBZ          0161400

#define DATAPTR         u6

#define RPEC1           016
/* RPEC1 - 16 - ECC status 1 - unimplemented */
#define RPEC2           017
/* RPEC2 - 17 - ECC status 2 - unimplemented */


/* This controller supports many different disk drive types.  These drives
   are operated in 576 bytes/sector (128 36b words/sector) mode, which gives
   them somewhat different geometry from the PDP-11 variants:

   type         #sectors/       #surfaces/      #cylinders/
                 surface         cylinder        drive

   RP04/5       20              19              411             =88MB
   RP06         20              19              815             =176MB
   RP07         43              32              630             =516MB

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE.

   The RP07, despite its name, uses an RM-style controller.
*/

#define RP04_DTYPE      0
#define RP04_SECT       20
#define RP04_SURF       19
#define RP04_CYL        411
#define RP04_DEV        020020
#define RP04_SIZE       (RP04_SECT * RP04_SURF * RP04_CYL * RP_NUMWD)

#define RP06_DTYPE      1
#define RP06_SECT       20
#define RP06_SURF       19
#define RP06_CYL        815
#define RP06_DEV        020022
#define RP06_SIZE       (RP06_SECT * RP06_SURF * RP06_CYL * RP_NUMWD)

#define RP07_DTYPE      2
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
    };

struct drvtyp rp_drv_tab[] = {
    { RP04_SECT, RP04_SURF, RP04_CYL, RP04_SIZE, RP04_DEV },
    { RP06_SECT, RP06_SURF, RP06_CYL, RP06_SIZE, RP06_DEV },
    { RP07_SECT, RP07_SURF, RP07_CYL, RP07_SIZE, RP07_DEV },
    { 0 }
    };


t_stat        rp_devio(uint32 dev, uint64 *data);
int           rp_devirq(uint32 dev, int addr);
int           rp_write(DEVICE *dptr, struct rh_if *rh, int reg, uint32 data);
int           rp_read(DEVICE *dptr, struct rh_if *rh, int reg, uint32 *data);
void          rp_rst(DEVICE *dptr);
t_stat        rp_svc(UNIT *);
t_stat        rp_boot(int32, DEVICE *);
void          rp_ini(UNIT *, t_bool);
t_stat        rp_reset(DEVICE *);
t_stat        rp_attach(UNIT *, CONST char *);
t_stat        rp_detach(UNIT *);
t_stat        rp_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat        rp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                    const char *cptr);
const char    *rp_description (DEVICE *dptr);
uint64        rp_buf[NUM_DEVS_RP][RP_NUMWD];
#if KS
extern DEVICE *rh_boot_dev;
extern int     rh_boot_unit;
#endif


UNIT                rp_unit[] = {
/* Controller 1 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(0), RP06_SIZE) },
#if (NUM_DEVS_RP > 1)
/* Controller 2 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(1), RP06_SIZE) },
#if (NUM_DEVS_RP > 2)
/* Controller 3 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(2), RP06_SIZE) },
#if (NUM_DEVS_RP > 3)
/* Controller 4 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL_RH(3), RP06_SIZE) },
#endif
#endif
#endif
};

#if KS
struct rh_if   rp_rh[NUM_DEVS_RP] = {
     { &rp_write, &rp_read, &rp_rst},
};

DIB rp_dib[] = {
    {0776700, 077, 0254, 6, 1, &uba_rh_read, &uba_rh_write, 0, 0, &rp_rh[0]},
};
#else
struct rh_if   rp_rh[NUM_DEVS_RP] = {
     { &rp_write, &rp_read, &rp_rst},
     { &rp_write, &rp_read, &rp_rst},
     { &rp_write, &rp_read, &rp_rst},
     { &rp_write, &rp_read, &rp_rst}
};

DIB rp_dib[] = {
    {RH10_DEV, 1, &rh_devio, &rh_devirq, &rp_rh[0]},
    {RH10_DEV, 1, &rh_devio, &rh_devirq, &rp_rh[1]},
    {RH10_DEV, 1, &rh_devio, &rh_devirq, &rp_rh[2]},
    {RH10_DEV, 1, &rh_devio, &rh_devirq, &rp_rh[3]}};
#endif

MTAB                rp_mod[] = {
#if KL
    {MTAB_XTD|MTAB_VDV, TYPE_RH10, NULL, "RH10",  &rh_set_type, NULL, 
              NULL, "Sets controller to RH10" },
    {MTAB_XTD|MTAB_VDV, TYPE_RH20, "RH20", "RH20", &rh_set_type, &rh_show_type,
              NULL, "Sets controller to RH20"},
#endif
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
    {UNIT_DTYPE, (RP07_DTYPE << UNIT_V_DTYPE), "RP07", "RP07", &rp_set_type },
    {UNIT_DTYPE, (RP06_DTYPE << UNIT_V_DTYPE), "RP06", "RP06", &rp_set_type },
    {UNIT_DTYPE, (RP04_DTYPE << UNIT_V_DTYPE), "RP04", "RP04", &rp_set_type },
    {MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT", NULL, &disk_show_fmt },
#if KS
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "addr", "addr",  &uba_set_addr, uba_show_addr,
              NULL, "Sets address of RH11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "vect", "vect",  &uba_set_vect, uba_show_vect,
              NULL, "Sets vect of RH11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "br", "br",  &uba_set_br, uba_show_br,
              NULL, "Sets br of RH11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "ctl", "ctl",  &uba_set_ctl, uba_show_ctl,
              NULL, "Sets uba of RH11" },
#endif
    {0}
};

REG                 rpa_reg[] = {
#if !KS
    {ORDATA(IVECT, rp_rh[0].ivect, 18)},
    {FLDATA(IMODE, rp_rh[0].imode, 0)},
    {ORDATA(XFER, rp_rh[0].xfer_drive, 3), REG_HRO},
    {ORDATA(REG, rp_rh[0].reg, 6), REG_RO},
    {ORDATA(CIA, rp_rh[0].cia, 18)},
    {ORDATA(CCW, rp_rh[0].ccw, 18)},
    {ORDATA(DEVNUM, rp_rh[0].devnum, 9), REG_HRO},
#endif
    {ORDATA(DRIVE, rp_rh[0].drive, 3), REG_HRO},
    {ORDATA(RAE, rp_rh[0].rae, 8), REG_RO},
    {ORDATA(ATTN, rp_rh[0].attn, 8), REG_RO},
    {ORDATA(STATUS, rp_rh[0].status, 18), REG_RO},
    {ORDATA(WCR, rp_rh[0].wcr, 18)},
    {ORDATA(CDA, rp_rh[0].cda, 18)},
    {ORDATA(BUF, rp_rh[0].buf, 36), REG_HRO},
    {BRDATA(BUFF, rp_buf[0], 16, 64, RP_NUMWD), REG_HRO},
    {0}
};  

DEVICE              rpa_dev = {
    "RPA", rp_unit, rpa_reg, rp_mod,
    NUM_UNITS_RP, 8, 18, 1, 8, 36,
    NULL, NULL, &rp_reset, &rp_boot, &rp_attach, &rp_detach,
    &rp_dib[0], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rp_help, NULL, NULL, &rp_description
};

#if (NUM_DEVS_RP > 1)
REG                 rpb_reg[] = {
    {ORDATA(IVECT, rp_rh[1].ivect, 18)},
    {FLDATA(IMODE, rp_rh[1].imode, 0)},
    {ORDATA(XFER, rp_rh[1].xfer_drive, 3), REG_HRO},
    {ORDATA(REG, rp_rh[1].reg, 6), REG_RO},
    {ORDATA(CIA, rp_rh[1].cia, 18)},
    {ORDATA(CCW, rp_rh[1].ccw, 18)},
    {ORDATA(DEVNUM, rp_rh[1].devnum, 9), REG_HRO},
    {ORDATA(DRIVE, rp_rh[1].drive, 3), REG_HRO},
    {ORDATA(RAE, rp_rh[1].rae, 8), REG_RO},
    {ORDATA(ATTN, rp_rh[1].attn, 8), REG_RO},
    {ORDATA(STATUS, rp_rh[1].status, 18), REG_RO},
    {ORDATA(WCR, rp_rh[1].wcr, 18)},
    {ORDATA(CDA, rp_rh[1].cda, 18)},
    {ORDATA(BUF, rp_rh[1].buf, 36), REG_HRO},
    {BRDATA(BUFF, rp_buf[1], 16, 64, RP_NUMWD), REG_HRO},
    {0}
};  

DEVICE              rpb_dev = {
    "RPB", &rp_unit[010], rpb_reg, rp_mod,
    NUM_UNITS_RP, 8, 18, 1, 8, 36,
    NULL, NULL, &rp_reset, &rp_boot, &rp_attach, &rp_detach,
    &rp_dib[1], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rp_help, NULL, NULL, &rp_description
};

#if (NUM_DEVS_RP > 2)
REG                 rpc_reg[] = {
    {ORDATA(IVECT, rp_rh[2].ivect, 18)},
    {FLDATA(IMODE, rp_rh[2].imode, 0)},
    {ORDATA(XFER, rp_rh[2].xfer_drive, 3), REG_HRO},
    {ORDATA(CIA, rp_rh[2].cia, 18)},
    {ORDATA(CCW, rp_rh[2].ccw, 18)},
    {ORDATA(REG, rp_rh[2].reg, 6), REG_RO},
    {ORDATA(DEVNUM, rp_rh[2].devnum, 9), REG_HRO},
    {ORDATA(DRIVE, rp_rh[2].drive, 3), REG_HRO},
    {ORDATA(RAE, rp_rh[2].rae, 8), REG_RO},
    {ORDATA(ATTN, rp_rh[2].attn, 8), REG_RO},
    {ORDATA(STATUS, rp_rh[2].status, 18), REG_RO},
    {ORDATA(WCR, rp_rh[2].wcr, 18)},
    {ORDATA(CDA, rp_rh[2].cda, 18)},
    {ORDATA(BUF, rp_rh[2].buf, 36), REG_HRO},
    {BRDATA(BUFF, rp_buf[2], 16, 64, RP_NUMWD), REG_HRO},
    {0}
};  

DEVICE              rpc_dev = {
    "RPC", &rp_unit[020], rpc_reg, rp_mod,
    NUM_UNITS_RP, 8, 18, 1, 8, 36,
    NULL, NULL, &rp_reset, &rp_boot, &rp_attach, &rp_detach,
    &rp_dib[2], DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rp_help, NULL, NULL, &rp_description
};

#if (NUM_DEVS_RP > 3)
REG                 rpd_reg[] = {
    {ORDATA(IVECT, rp_rh[3].ivect, 18)},
    {FLDATA(IMODE, rp_rh[3].imode, 0)},
    {ORDATA(XFER, rp_rh[3].xfer_drive, 3), REG_HRO},
    {ORDATA(REG, rp_rh[3].reg, 6), REG_RO},
    {ORDATA(DRIVE, rp_rh[3].drive, 3), REG_HRO},
    {ORDATA(RAE, rp_rh[3].rae, 8), REG_RO},
    {ORDATA(ATTN, rp_rh[3].attn, 8), REG_RO},
    {ORDATA(STATUS, rp_rh[3].status, 18), REG_RO},
    {ORDATA(CIA, rp_rh[3].cia, 18)},
    {ORDATA(CCW, rp_rh[3].ccw, 18)},
    {ORDATA(WCR, rp_rh[3].wcr, 18)},
    {ORDATA(CDA, rp_rh[3].cda, 18)},
    {ORDATA(DEVNUM, rp_rh[3].devnum, 9), REG_HRO},
    {ORDATA(BUF, rp_rh[3].buf, 36), REG_HRO},
    {BRDATA(BUFF, rp_buf[3], 16, 64, RP_NUMWD), REG_HRO},
    {0}
};  

DEVICE              rpd_dev = {
    "RPD", &rp_unit[030], rpd_reg, rp_mod,
    NUM_UNITS_RP, 8, 18, 1, 8, 36,
    NULL, NULL, &rp_reset, &rp_boot, &rp_attach, &rp_detach,
    &rp_dib[3], DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rp_help, NULL, NULL, &rp_description
};

#endif
#endif
#endif

DEVICE *rp_devs[] = {
    &rpa_dev,
#if (NUM_DEVS_RP > 1)
    &rpb_dev,
#if (NUM_DEVS_RP > 2)
    &rpc_dev,
#if (NUM_DEVS_RP > 3)
    &rpd_dev,
#endif
#endif
#endif
};


void
rp_rst(DEVICE *dptr)
{
    UNIT     *uptr = dptr->units;
    uint16   *regs;
    int       ctlr = GET_CNTRL_RH(uptr->flags);
    uint32    i;

    rh_reset(dptr, &rp_rh[ctlr]);
    for (i = 0; i < dptr->numunits; i++) {
        regs = (uint16 *)(uptr->up7);
        regs[RPDS] &= DS_VV;
        if (regs[RPMR] & 1) {
             uptr->CCYL = GET_CY;
             regs[RPOF] &= OF_HCI|OF_ECI|OF_F22;
        }
        if ((uptr->flags & UNIT_ATT) != 0)                  /* attached? */
           regs[RPDS] |= DS_DRY;
        regs[RPER1] = 0;
        regs[RPER2] = 0;
        regs[RPER3] = 0;
        uptr++;
    }
}

int
rp_write(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 data) {
    int            i;
    int            unit = rhc->drive;
    UNIT          *uptr = &dptr->units[unit];
    int            dtype = GET_DTYPE(uptr->flags);
    uint16        *regs = (uint16 *)(uptr->up7);

    if ((uptr->flags & UNIT_DIS) != 0 && reg != 04)
        return 1;
    if ((regs[RPCS1] & CS1_GO) && reg != 04) {
        regs[RPER1] |= ER1_RMR;
        return 0;
    }
    switch(reg) {
    case  000:  /* control */
        sim_debug(DEBUG_DETAIL, dptr, "%s%o Status=%06o\n", dptr->name, unit, regs[RPCS1]);
        /* If drive not ready don't do anything */
        if ((regs[RPCS1] & CS1_GO) != 0) {
           regs[RPER1] |= ER1_RMR;
           sim_debug(DEBUG_DETAIL, dptr, "%s%o not ready\n", dptr->name, unit);
           return 0;
        }
        /* Check if GO bit set */
        if ((data & 1) == 0) {
           regs[RPCS1] = data & 076;
           sim_debug(DEBUG_DETAIL, dptr, "%s%o no go %06o\n", dptr->name, unit, data);
           return 0;                           /* No, nop */
        }
        regs[RPDS] &= DS_ATA|DS_VV|DS_OFF;
        regs[RPCS1] = data & 076;
        switch (GET_FNC(data)) {
        case FNC_NOP:
            regs[RPDS] |= DS_DRY;
            break;

        case FNC_RECAL:                       /* recalibrate */
            regs[RPDC] = 0;
             /* Fall through */

        case FNC_RETURN:                      /* return to center */
        case FNC_OFFSET:                      /* offset */
        case FNC_UNLOAD:                      /* unload */
            regs[RPDS] &= ~DS_OFF;
             /* Fall through */

        case FNC_SEARCH:                      /* search */
        case FNC_SEEK:                        /* seek */
        case FNC_WCHK:                        /* write check */
        case FNC_WRITE:                       /* write */
        case FNC_WRITEH:                      /* write w/ headers */
        case FNC_READ:                        /* read */
        case FNC_READH:                       /* read w/ headers */

            if (GET_CY >= rp_drv_tab[dtype].cyl ||
                GET_SC(regs[RPDA]) >= rp_drv_tab[dtype].sect ||
                GET_SF(regs[RPDA]) >= rp_drv_tab[dtype].surf) {
                rhc->attn &= ~(1<<unit);
                regs[RPDS] |= DS_ATA|DS_DRY;
                regs[RPER1] |= ER1_IAE;
                break;
            }
            regs[RPDS] |= DS_PIP;
            regs[RPDS] &= ~DS_DRY;
            regs[RPCS1] |= CS1_GO;
            CLR_BUF(uptr);
            uptr->DATAPTR = 0;
            break;


        case FNC_DCLR:                        /* drive clear */
            regs[RPDS] &= ~DS_ATA;
            regs[RPDS] |= DS_DRY;
            regs[RPOF] = 0;
            regs[RPER1] = 0;
            regs[RPER2] = 0;
            regs[RPER3] = 0;
#if KS
            rhc->error = 0;
#endif
            rhc->attn &= ~(1<<unit);
            break;

        case FNC_PRESET:                      /* read-in preset */
            regs[RPDA] = 0;
            regs[RPDC] = 0;
            regs[RPDS] &= ~DS_OFF;
            regs[RPDS] |= DS_DRY;
            regs[RPOF] = 0;
             /* Fall through */

        case FNC_RELEASE:                     /* port release */
        case FNC_PACK:                        /* pack acknowledge */
            if ((uptr->flags & UNIT_ATT) != 0)
                regs[RPDS] |= DS_VV|DS_DRY;
            break;

        default:
            regs[RPDS] |= DS_ATA|DS_DRY;
            regs[RPER1] |= ER1_ILF;
            rhc->attn |= (1<<unit);
        }
        if (regs[RPCS1] & CS1_GO)
            sim_activate(uptr, 1000);
        sim_debug(DEBUG_DETAIL, dptr, "%s%o AStatus=%06o\n", dptr->name, unit, regs[RPCS1]);
        return 0;
    case  001:  /* status */
        break;
    case  002:  /* error register 1 */
        regs[RPER1] = data;
        break;
    case  003:  /* maintenance */
        /* RPLA has 10 bits for position in sector */
        if (data & 1) {
            if ((data & 076) == 0) {
                if (regs[RPMR] & 010)
                   regs[RPLA] ++;
                if (regs[RPMR] & 04)
                   regs[RPLA] = 0;
            }
            regs[RPMR] = data;
            fprintf(stderr, "Write %o MR=%06o\r\n", unit, data);
        } else
            regs[RPMR] = 0;
        break;
    case  004:  /* atten summary */
        for (i = 0; i < 8; i++) {
            if (data & (1<<i)) {
                UNIT      *u = &dptr->units[i];
                uint16    *r = (uint16 *)(u->up7);
                r[RPDS] &= ~DS_ATA;
                rhc->attn &= ~(1<<i);
            }
        }
        break;
    case  005:  /* sector/track */
        regs[RPDA] = data;
        break;
    case  014:  /* error register 2 */
        regs[RPER2] = data;
        break;
    case  006:  /* drive type */
    case  007:  /* look ahead */
        break;
    case  011:  /* offset */
        regs[RPOF] = data & 0016277;
        break;
    case  012:  /* desired cylinder */
        regs[RPDC] = (data & DC_M_CY) << DC_V_CY;
        break;
    case  015:  /* error register 3 */
        regs[RPER3] = data & 076210;
        break;
    case  013:  /* current cylinder */
    case  010:  /* serial no */
    case  016:  /* ecc position */
    case  017:  /* ecc pattern */
        break;
    default:
        regs[RPER1] |= ER1_ILR;
        rhc->rae |= 1 << unit;
    }
    return 0;
}

int
rp_read(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 *data) {
    int           unit = rhc->drive;
    UNIT          *uptr = &dptr->units[unit];
    uint16        *regs = (uint16 *)(uptr->up7);
    uint32        temp = 0;
    int           i;

    if ((uptr->flags & UNIT_DIS) != 0 && reg != 04)
        return 1;
    if ((uptr->flags & UNIT_ATT) == 0 && reg != 04) {    /* not attached? */
        *data = 0;
        return 0;
    }
    switch(reg) {
    case  000:  /* control */
        temp = regs[RPCS1];
        if (uptr->flags & UNIT_ATT)
           temp |= CS1_DVA;
        break;
    case  001:  /* status */
        temp = regs[RPDS];
        if ((regs[RPER1] | regs[RPER2] | regs[RPER3]) != 0)
           temp |= DS_ERR;
        if ((uptr->flags & UNIT_DIS) == 0)
           temp |= DS_DPR;
        if ((uptr->flags & UNIT_ATT) != 0)
           temp |= DS_MOL;
        if ((uptr->flags & UNIT_WPRT) != 0)
           temp |= DS_WRL;
        break;
    case  002:  /* error register 1 */
        temp = regs[RPER1];
        break;
    case  003:  /* maintenance */
        if ((regs[RPMR] & 1) != 0)
            temp = regs[RPMR];
        break;
    case  004:  /* atten summary */
        for (i = 0; i < 8; i++) {
            UNIT      *u = &dptr->units[i];
            uint16    *r = (uint16 *)(u->up7);
            if (r[RPDS] & DS_ATA) {
                temp |= 1 << i;
            }
        }
        break;
    case  005:  /* sector/track */
        temp = regs[RPDA];
        break;
    case  006:  /* drive type */
        temp = rp_drv_tab[GET_DTYPE(uptr->flags)].devtype;
        break;
    case  011:  /* offset */
        temp = regs[RPOF];
        break;
    case  012:  /* desired cylinder */
        temp = regs[RPDC];
        break;
    case  013:  /* current cylinder */
        temp = uptr->CCYL;
        break;
    case  010:  /* serial no */
        i = GET_CNTRL_RH(uptr->flags);
        temp = (020 * i) + (unit + 1);
        break;
    case  014:  /* error register 2 */
        temp = regs[RPER2];
        break;
    case  015:  /* error register 3 */
        temp = regs[RPER3];
        break;
    case  007:  /* look ahead */
        if ((regs[RPLA] >> 10) >= 23)
            regs[RPLA] = 0;
        temp = GET_SC(regs[RPDA]) << 6;
        temp ^= ((regs[RPLA] + 1) >> 4) & 07760;
        if ((regs[RPMR] & 1) == 0)
            regs[RPLA] += 1024; 
        break;
    case  016:  /* ecc position */
    case  017:  /* ecc pattern */
        break;
    default:
        regs[RPER1] |= ER1_ILR;
        rhc->rae |= 1 << unit;
    }
    *data = temp;
    return 0;
}


t_stat rp_svc (UNIT *uptr)
{
    int           dtype = GET_DTYPE(uptr->flags);
    int           ctlr = GET_CNTRL_RH(uptr->flags);
    uint16        *regs = (uint16 *)(uptr->up7);
    int           cyl = GET_CY;
    int           unit;
    DEVICE       *dptr;
    struct rh_if *rhc;
    int           diff, da;
    int           sts;

    dptr = rp_devs[ctlr];
    rhc = &rp_rh[ctlr];
    unit = uptr - dptr->units;
    if ((uptr->flags & UNIT_ATT) == 0) {                  /* not attached? */
        regs[RPDS] |= DS_ATA;                           /* set drive error */
        regs[RPDS] &= ~DS_DRY;
        regs[RPER1] |= ER1_UNS;                            /* set drive error */
        if (GET_FNC(regs[RPCS1]) >= FNC_XFER) {             /* xfr? set done */
           rh_setirq(rhc);
        } else {
           rh_setattn(rhc, unit);
        }
        return (SCPE_OK);
    }

    /* Check if seeking */
    if (regs[RPDS] & DS_PIP) {
        sim_debug(DEBUG_DETAIL, dptr, "%s%o seek %d %d\n", dptr->name, unit, cyl, uptr->CCYL);
        if (cyl >= rp_drv_tab[dtype].cyl) {
            regs[RPDS] &= ~DS_PIP;
            regs[RPDS] |= (DS_ATA|DS_DRY);
            regs[RPCS1] &= ~CS1_GO;
            regs[RPER1] |= ER1_IAE;
            rh_setattn(rhc, unit);
        }
        diff = cyl - (uptr->CCYL & 01777);
        if (diff < 0) {
            if (diff < -50) {
                uptr->CCYL -= 50;
                sim_activate(uptr, 500);
            } else if (diff < -10) {
                uptr->CCYL -= 10;
                sim_activate(uptr, 200);
            } else {
                uptr->CCYL -= 1;
                sim_activate(uptr, 100);
            }
            return SCPE_OK;
        } else if (diff > 0) {
            if (diff > 50) {
                uptr->CCYL += 50;
                sim_activate(uptr, 500);
            } else if (diff > 10) {
                uptr->CCYL += 10;
                sim_activate(uptr, 200);
            } else {
                uptr->CCYL += 1;
                sim_activate(uptr, 100);
            }
            return SCPE_OK;
        } else {
            regs[RPDS] &= ~DS_PIP;
            uptr->DATAPTR = 0;
        }
    }

    switch (GET_FNC(regs[RPCS1])) {
    case FNC_NOP:
    case FNC_DCLR:                       /* drive clear */
    case FNC_RELEASE:                    /* port release */
    case FNC_PACK:                       /* pack acknowledge */
        break;
    case FNC_UNLOAD:                     /* unload */
        rp_detach(uptr);
        regs[RPDS] |= DS_ATA|DS_DRY;
        regs[RPCS1] &= ~CS1_GO;
        rh_setattn(rhc, unit);
        sim_debug(DEBUG_DETAIL, dptr, "%s%o unload %d %o\n", dptr->name, unit, cyl, regs[RPCS1]);
        break;
    case FNC_OFFSET:                     /* offset */
        regs[RPDS] |= DS_OFF;
        /* Fall through */
    case FNC_RETURN:                     /* return to center */
    case FNC_PRESET:                     /* read-in preset */
    case FNC_RECAL:                      /* recalibrate */
    case FNC_SEEK:                       /* seek */
        if (GET_SC(regs[RPDA]) >= rp_drv_tab[dtype].sect ||
            GET_SF(regs[RPDA]) >= rp_drv_tab[dtype].surf)
            regs[RPER1] |= ER1_IAE;
        regs[RPDS] |= DS_ATA|DS_DRY;
        regs[RPCS1] &= ~CS1_GO;
        rh_setattn(rhc, unit);
        sim_debug(DEBUG_DETAIL, dptr, "%s%o seekdone %d %o\n", dptr->name, unit, cyl, regs[RPCS1]);
        break;

    case FNC_SEARCH:                     /* search */
        if (GET_SC(regs[RPDA]) >= rp_drv_tab[dtype].sect ||
            GET_SF(regs[RPDA]) >= rp_drv_tab[dtype].surf)
            regs[RPER1] |= ER1_IAE;
        regs[RPDS] |= DS_ATA|DS_DRY;
        regs[RPCS1] &= ~CS1_GO;
        rh_setattn(rhc, unit);
        sim_debug(DEBUG_DETAIL, dptr, "%s%o searchdone %d %o\n", dptr->name, unit, cyl, regs[RPCS1]);
        break;

    case FNC_READ:                       /* read */
    case FNC_READH:                      /* read w/ headers */
    case FNC_WCHK:                       /* write check */
        if (regs[RPER1] != 0) {
            sim_debug(DEBUG_DETAIL, dptr, "%s%o read error\n", dptr->name, unit);
            goto rd_end;
        }

        if (BUF_EMPTY(uptr)) {
            if (GET_SC(regs[RPDA]) >= rp_drv_tab[dtype].sect ||
                GET_SF(regs[RPDA]) >= rp_drv_tab[dtype].surf) {
                regs[RPER1] |= ER1_IAE;
                regs[RPDS] |= DS_ATA|DS_DRY;
                regs[RPCS1] &= ~CS1_GO;
                rh_finish_op(rhc, 0);
                sim_debug(DEBUG_DETAIL, dptr, "%s%o readx done\n", dptr->name, unit);
                return SCPE_OK;
            }
            sim_debug(DEBUG_DETAIL, dptr, "%s%o read (%d,%d,%d)\n", dptr->name, unit, cyl,
                   GET_SF(regs[RPDA]), GET_SC(regs[RPDA]));
            da = GET_DA(dtype);
            (void)disk_read(uptr, &rp_buf[ctlr][0], da, RP_NUMWD);
            uptr->hwmark = RP_NUMWD;
            uptr->DATAPTR = 0;
            /* On read headers, transfer 2 words to start */
            if (GET_FNC(regs[RPCS1]) == FNC_READH) {
                rhc->buf = (((uint64)cyl) << 18) | 
                         ((uint64)((GET_SF(regs[RPDA]) << 8) | GET_SF(regs[RPDA])));
                sim_debug(DEBUG_DATA, dptr, "%s%o read word h1 %012llo %09o %06o\n",
                   dptr->name, unit, rhc->buf, rhc->cda, rhc->wcr);
                if (rh_write(rhc) == 0)
                    goto rd_end;
                rhc->buf = ((uint64)((020 * ctlr) + (unit + 1)) << 18) | (uint64)(unit);
                sim_debug(DEBUG_DATA, dptr, "%s%o read word h2 %012llo %09o %06o\n",
                   dptr->name, unit, rhc->buf, rhc->cda, rhc->wcr);
                if (rh_write(rhc) == 0)
                    goto rd_end;
            }
        }

        rhc->buf = rp_buf[ctlr][uptr->DATAPTR++];
        sim_debug(DEBUG_DATA, dptr, "%s%o read word %d %012llo %09o %06o\n",
                   dptr->name, unit, uptr->DATAPTR, rhc->buf, rhc->cda, rhc->wcr);
        if (rh_write(rhc)) {
            if (uptr->DATAPTR == RP_NUMWD) {
                /* Increment to next sector. Set Last Sector */
                uptr->DATAPTR = 0;
                CLR_BUF(uptr);
                regs[RPDA] += 1 << DA_V_SC;
                if (GET_SC(regs[RPDA]) >= rp_drv_tab[dtype].sect) {
                    regs[RPDA] &= (DA_M_SF << DA_V_SF);
                    regs[RPDA] += 1 << DA_V_SF;
                    if (GET_SF(regs[RPDA]) >= rp_drv_tab[dtype].surf) {
                         regs[RPDA] = 0;
                         regs[RPDC] += 1 << DC_V_CY;
                         regs[RPDS] |= DS_PIP;
                    }
                }
                if (rh_blkend(rhc))
                    goto rd_end;
            }
            sim_activate(uptr, 10);
        } else {
rd_end:
            sim_debug(DEBUG_DETAIL, dptr, "%s%o read done\n", dptr->name, unit);
            regs[RPCS1] &= ~CS1_GO;
            regs[RPDS] &= ~DS_PIP;
            regs[RPDS] |= DS_DRY;
            if (uptr->DATAPTR == RP_NUMWD) 
               (void)rh_blkend(rhc);
            rh_finish_op(rhc, 0);
            return SCPE_OK;
        }
        break;

    case FNC_WRITE:                      /* write */
    case FNC_WRITEH:                     /* write w/ headers */
        if (regs[RPER1] != 0) {
            sim_debug(DEBUG_DETAIL, dptr, "%s%o read error\n", dptr->name, unit);
            goto wr_end;
        }

        if (BUF_EMPTY(uptr)) {
            if (GET_SC(regs[RPDA]) >= rp_drv_tab[dtype].sect ||
                GET_SF(regs[RPDA]) >= rp_drv_tab[dtype].surf) {
                regs[RPER1] |= ER1_IAE;
                regs[RPDS] |= DS_ATA|DS_DRY;
                regs[RPCS1] &= ~CS1_GO;
                rh_finish_op(rhc, 0);
                sim_debug(DEBUG_DETAIL, dptr, "%s%o writex done\n", dptr->name, unit);
                return SCPE_OK;
            }
            /* On Write headers, transfer 2 words to start */
            if (GET_FNC(regs[RPCS1]) == FNC_WRITEH) {
                if (rh_read(rhc) == 0)
                    goto wr_end;
                sim_debug(DEBUG_DATA, dptr, "%s%o write word h1 %012llo %06o\n",
                      dptr->name, unit, rhc->buf, rhc->wcr);
                if (rh_read(rhc) == 0)
                    goto wr_end;
                sim_debug(DEBUG_DATA, dptr, "%s%o write word h2 %012llo %06o\n",
                      dptr->name, unit, rhc->buf, rhc->wcr);
            }
            uptr->DATAPTR = 0;
            uptr->hwmark = 0;
        }
        sts = rh_read(rhc);
        sim_debug(DEBUG_DATA, dptr, "%s%o write word %d %012llo %06o %06o\n",
                      dptr->name, unit, uptr->DATAPTR, rhc->buf, rhc->cda, rhc->wcr);
        rp_buf[ctlr][uptr->DATAPTR++] = rhc->buf;
        if (sts == 0) {
            while (uptr->DATAPTR < RP_NUMWD)
                rp_buf[ctlr][uptr->DATAPTR++] = 0;
        }
        if (uptr->DATAPTR == RP_NUMWD) {
            sim_debug(DEBUG_DETAIL, dptr, "%s%o write (%d,%d,%d)\n", dptr->name,
                   unit, cyl, GET_SF(regs[RPDA]), GET_SC(regs[RPDA]));
            da = GET_DA(dtype);
            (void)disk_write(uptr, &rp_buf[ctlr][0], da, RP_NUMWD);
            uptr->DATAPTR = 0;
            CLR_BUF(uptr);
            if (sts) {
                regs[RPDA] += 1 << DA_V_SC;
                if (GET_SC(regs[RPDA]) >= rp_drv_tab[dtype].sect) {
                    regs[RPDA] &= (DA_M_SF << DA_V_SF);
                    regs[RPDA] += 1 << DA_V_SF;
                    if (GET_SF(regs[RPDA]) >= rp_drv_tab[dtype].surf) {
                         regs[RPDA] = 0;
                         regs[RPDC] += 1 << DC_V_CY;
                         regs[RPDS] |= DS_PIP;
                    }
                }
            }
            if (rh_blkend(rhc))
               goto wr_end;
        }
        if (sts) {
            sim_activate(uptr, 10);
        } else {
wr_end:
            sim_debug(DEBUG_DETAIL, dptr, "RP%o write done\n", unit);
            regs[RPCS1] &= ~CS1_GO;
            regs[RPDS] |= DS_DRY;
            rh_finish_op(rhc, 0);
            return SCPE_OK;
        }
        break;
    }
    return SCPE_OK;
}


t_stat
rp_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int         i;

    if (uptr == NULL) return SCPE_IERR;
    uptr->flags &= ~(UNIT_DTYPE);
    uptr->flags |= val;
    i = GET_DTYPE(val);
    uptr->capac = rp_drv_tab[i].size;
    return SCPE_OK;
}


t_stat
rp_reset(DEVICE * rptr)
{
    UNIT     *uptr = rptr->units;
    uint16   *regs;
    int       ctlr = GET_CNTRL_RH(uptr->flags);
    uint32    i;

    rh_reset(rptr, &rp_rh[ctlr]);
    for (i = 0; i < rptr->numunits; i++) {
        if (uptr->up7 == 0 && (uptr->up7 = calloc(16, sizeof(uint16))) == 0)
            return SCPE_IERR;
        regs = (uint16 *)(uptr->up7);
        regs[RPDS] &= DS_VV;
        if ((uptr->flags & UNIT_ATT) != 0)                  /* attached? */
           regs[RPDS] |= DS_DRY;
        if (regs[RPMR] & 1)
            uptr->CCYL = GET_CY;
        regs[RPER1] = 0;
        regs[RPER2] = 0;
        regs[RPER3] = 0;
        regs[RPDC] = 0;
        regs[RPMR] = 0;
        uptr++;
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
rp_boot(int32 unit_num, DEVICE * rptr)
{
    UNIT         *uptr = &rptr->units[unit_num];
    int           ctlr = GET_CNTRL_RH(uptr->flags);
    int           dtype = GET_DTYPE(uptr->flags);
    uint16        *regs = (uint16 *)(uptr->up7);
    struct rh_if *rhc = &rp_rh[ctlr];
    DEVICE       *dptr = uptr->dptr;
    uint32        addr;
    uint32        ptr = 0;
    uint64        word;
#if !KS
    int           wc;
#endif

#if KS
    int           da;
    t_stat        r;
    uint64        len;

    if ((r = rp_reset(dptr)) != SCPE_OK)
        return r;
    if ((r = cty_reset(&cty_dev)) != SCPE_OK)
        return r;
    /* Read in block 1 and see if it is a home block */
    disk_read(uptr, &rp_buf[0][0], 1, RP_NUMWD);
    if (rp_buf[0][0] != 0505755000000LL) {
        /* Try blocks 10 and 12 if fail */
        disk_read(uptr, &rp_buf[0][0], 010, RP_NUMWD);
        if (rp_buf[0][0] != 0505755000000LL) {
           disk_read(uptr, &rp_buf[0][0], 012, RP_NUMWD);
           if (rp_buf[0][0] != 0505755000000LL)
              return SCPE_IERR;
        }
    }

    /* Word 103 and 102 contain pointer to SMFILE block */
    regs[RPDA] = (int32)((rp_buf[0][0103] & 077) << DA_V_SC) |
               (int32)(((rp_buf[0][0103] >> 8) & 077) << DA_V_SF);
    regs[RPDC] =  (int32)((rp_buf[0][0103] >> 24) << DC_V_CY);
    len = (int)(rp_buf[0][0102] & RMASK);
    da = GET_DA(dtype);
    disk_read(uptr, &rp_buf[0][0], da, RP_NUMWD);
    /* For diagnostics use locations 6 and 7 */
    if (sim_switches & SWMASK ('D')) {
       sim_messagef(SCPE_OK, "Diags boot\n");
       regs[RPDA] = (int32)((rp_buf[0][06] & 077) << DA_V_SC) |
                  (int32)(((rp_buf[0][06] >> 8) & 077) << DA_V_SF);
       regs[RPDC] = (int32)((rp_buf[0][06] >> 24) << DC_V_CY);
       len = (int)(((rp_buf[0][07] & 077) * 4) & RMASK);
    } else {
    /* Normal is at 4 and 5*/
       regs[RPDA] = (int32)((rp_buf[0][04] & 077) << DA_V_SC) |
                  (int32)(((rp_buf[0][04] >> 8) & 077) << DA_V_SF);
       regs[RPDC] =  (int32)((rp_buf[0][04] >> 24) << DC_V_CY);
       len = (int)(((rp_buf[0][05] & 077) * 4) & RMASK);
    }
if (len == 0)
    len = 4;
    /* Read len sectors into address 1000 */
    addr = 01000;
    for (; len > 0; len--) {
        int           i;

        da = GET_DA(dtype);
        disk_read(uptr, &rp_buf[0][0], da, RP_NUMWD);
        for (i = 0; i < RP_NUMWD; i++) {
            M[addr++] = rp_buf[0][i];
        }
        regs[RPDA] += 1 << DA_V_SC;
        if (GET_SC(regs[RPDA]) >= rp_drv_tab[dtype].sect) {
            regs[RPDA] &= (DA_M_SF << DA_V_SF);
            regs[RPDA] += 1 << DA_V_SF;
            if (GET_SF(regs[RPDA]) >= rp_drv_tab[dtype].surf) {
                 regs[RPDC] += 1 << DC_V_CY;
            }
        }
    }
    /* Start location, and set up load info */
    word = 01000;
    M[036] = rhc->dib->uba_addr | (rhc->dib->uba_ctl << 18);
    M[037] =  unit_num;
    rh_boot_dev = rptr;
    rh_boot_unit = unit_num;
#elif KL
    int           sect;
    /* KL does not support readin, so fake it by reading in sectors 4 to 7 */
    /* Possible in future find boot loader in FE file system */
    addr = (MEMSIZE - 512) & RMASK;
    for (sect = 4; sect <= 7; sect++) {
        disk_read(uptr, &rp_buf[0][0], sect, RP_NUMWD);
        ptr = 0;
        for(wc = RP_NUMWD; wc > 0; wc--) {
            word = rp_buf[0][ptr++];
            M[addr++] = word;
        }
    }
    word = (MEMSIZE - 512) & RMASK;
#else
    disk_read(uptr, &rp_buf[0][0], 0, RP_NUMWD);
    addr = rp_buf[0][ptr] & RMASK;
    wc = (rp_buf[0][ptr++] >> 18) & RMASK;
    while (wc != 0) {
        wc = (wc + 1) & RMASK;
        addr = (addr + 1) & RMASK;
        word = rp_buf[0][ptr++];
        if (addr < 020)
           FM[addr] = word;
        else
           M[addr] = word;
    }
    addr = rp_buf[0][ptr] & RMASK;
    wc = (rp_buf[0][ptr++] >> 18) & RMASK;
    word = rp_buf[0][ptr++];
#endif
#if !KS
    rhc->reg = 040;
    rhc->status |= PI_ENABLE;
#endif
    rhc->drive = uptr - dptr->units;
    PC = word & RMASK;
    regs[RPDS] |= DS_VV;
    return SCPE_OK;
}

/* Device attach */

t_stat rp_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat         r;
    DEVICE        *rptr;
    int            ctlr;
    uint16        *regs = (uint16 *)(uptr->up7);

    uptr->capac = rp_drv_tab[GET_DTYPE (uptr->flags)].size;
    r = disk_attach (uptr, cptr);
    if (r != SCPE_OK)
        return r;
    rptr = find_dev_from_unit(uptr);
    if (rptr == 0)
        return SCPE_OK;
#if KS
    ctlr = 0;
#else
    for (ctlr = 0; rh[ctlr].dev_num != 0; ctlr++) {
        if (rh[ctlr].dev == rptr)
            break;
    }
#endif
    if (sim_switches & SIM_SW_REST)
        return SCPE_OK;
    regs[RPDA] = 0;
    regs[RPDS] &= ~DS_VV;
    regs[RPDS] |= DS_DRY;
    rh_setirq(&rp_rh[ctlr]);
    return SCPE_OK;
}

/* Device detach */

t_stat rp_detach (UNIT *uptr)
{
    uint16        *regs = (uint16 *)(uptr->up7);

    if (!(uptr->flags & UNIT_ATT))                          /* attached? */
        return SCPE_OK;
    if (sim_is_active (uptr))                              /* unit active? */
        sim_cancel (uptr);                                  /* cancel operation */
    regs[RPDS] &= ~(DS_VV|DS_DRY);
    return disk_detach (uptr);
}

t_stat rp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RP04/05/06/07 Disk Pack Drives (RP)\n\n");
fprintf (st, "The RP controller implements the Massbus family of large disk drives.  RP\n");
fprintf (st, "options include the ability to set units write enabled or write locked, to\n");
fprintf (st, "set the drive type to one of six disk types or autosize, and to write a DEC\n");
fprintf (st, "standard 044 compliant bad block table on the last track.\n\n");
disk_attach_help(st, dptr, uptr, flag, cptr);
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The RP device supports the BOOT command.\n");
#if KS
fprintf (st, "The RH11 is a unibus device, various parameters can be changed on these devices\n");
fprintf (st, "\n The address of the device can be set with: \n");
fprintf (st, "      sim> SET RPA ADDR=octal   default address= 776700\n");
fprintf (st, "\n The interrupt vector can be set with: \n");
fprintf (st, "      sim> SET RPA VECT=octal   default 254\n");
fprintf (st, "\n The interrupt level can be set with: \n");
fprintf (st, "      sim> SET RPA BR=#     # should be between 4 and 7.\n");
fprintf (st, "\n The unibus addaptor that the DZ is on can be set with:\n");
fprintf (st, "      sim> SET RPA CTL=#    # can be either 1 or 3\n");
#endif
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *rp_description (DEVICE *dptr)
{
    return "RP04/05/06/07 Massbus disk controller";
}


#endif
