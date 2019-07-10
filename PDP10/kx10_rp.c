/* ka10_rp.c: Dec RH10 RP04/5/6

   Copyright (c) 2013-2017, Richard Cornwell

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

#ifndef NUM_DEVS_RP
#define NUM_DEVS_RP 0
#endif

#if (NUM_DEVS_RP > 0)
#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

#define RP_NUMWD        128     /* 36bit words/sec */
#define RP_DEVNUM       0270    /* First device number */
#define NUM_UNITS_RP    8

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    7
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define DTYPE(x)        (((x) & UNIT_M_DTYPE) << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */
#define CNTRL_V_CTYPE   (UNIT_V_UF + 4)
#define CNTRL_M_CTYPE   7
#define GET_CNTRL(x)    (((x) >> CNTRL_V_CTYPE) & CNTRL_M_CTYPE)
#define CNTRL(x)        (((x) & CNTRL_M_CTYPE) << CNTRL_V_CTYPE)

/* Parameters in the unit descriptor */

/* CONI Flags */
#define IADR_ATTN       0000000000040LL   /* Interrupt on attention */
#define IARD_RAE        0000000000100LL   /* Interrupt on register access error */
#define DIB_CBOV        0000000000200LL   /* Control bus overrun */
#define CXR_PS_FAIL     0000000002000LL   /* Power supply fail (not implemented) */
#define CXR_ILC         0000000004000LL   /* Illegal function code */
#define CR_DRE          0000000010000LL   /* Or Data and Control Timeout */
#define DTC_OVER        0000000020000LL   /* DF10 did not supply word on time (not implemented) */
#define CCW_COMP_1      0000000040000LL   /* Control word written. */
#define CXR_CHAN_ER     0000000100000LL   /* Channel Error */
#define CXR_EXC         0000000200000LL   /* Error in drive transfer */
#define CXR_DBPE        0000000400000LL   /* Device Parity error (not implemented) */
#define CXR_NXM         0000001000000LL   /* Channel non-existent memory (not implemented) */
#define CXR_CWPE        0000002000000LL   /* Channel Control word parity error (not implemented) */
#define CXR_CDPE        0000004000000LL   /* Channel Data Parity Error (not implemented) */
#define CXR_SD_RAE      0000200000000LL   /* Register access error */
#define CXR_ILFC        0000400000000LL   /* Illegal CXR function code */
#define B22_FLAG        0004000000000LL   /* 22 bit channel */
#define CC_CHAN_PLS     0010000000000LL   /* Channel transfer pulse (not implemented) */
#define CC_CHAN_ACT     0020000000000LL   /* Channel in use */
#define CC_INH          0040000000000LL   /* Disconnect channel */
#define CB_FULL         0200000000000LL   /* Set when channel buffer is full (not implemented) */
#define AR_FULL         0400000000000LL   /* Set when AR is full (not implemented) */

/* CONO Flags */
#define ATTN_EN         0000000000040LL   /* enable attention interrupt. */
#define REA_EN          0000000000100LL   /* enable register error interrupt */
#define CBOV_CLR        0000000000200LL   /* Clear CBOV */
#define CONT_RESET      0000000002000LL   /* Clear All error bits */
#define ILC_CLR         0000000004000LL   /* Clear ILC and SD RAE */
#define DRE_CLR         0000000010000LL   /* Clear CR_CBTO and CR_DBTO */
#define OVER_CLR        0000000020000LL   /* Clear DTC overrun */
#define WRT_CW          0000000040000LL   /* Write control word */
#define CHN_CLR         0000000100000LL   /* Clear Channel Error */
#define DR_EXC_CLR      0000000200000LL   /* Clear DR_EXC */
#define DBPE_CLR        0000000400000LL   /* Clear CXR_DBPE */

/* DATAO/DATAI */
#define CR_REG          0770000000000LL   /* Register number */
#define LOAD_REG        0004000000000LL   /* Load register */
#define CR_MAINT_MODE   0000100000000LL   /* Maint mode... not implemented */
#define CR_DRIVE        0000007000000LL
#define CR_GEN_EVD      0000000400000LL   /* Enable Parity */
#define CR_DXES         0000000200000LL   /* Disable DXES errors  */
#define CR_INAD         0000000077600LL
#define CR_WTEVM        0000000000100LL   /* Verify Parity */
#define CR_FUNC         0000000000076LL
#define CR_GO           0000000000001LL

#define IRQ_VECT        0000000000177LL   /* Interupt vector */
#define IRQ_KI10        0000002000000LL
#define IRQ_KA10        0000001000000LL

#define CMD      u3
/* u3  low */
/* RPC - 00 - control */

#define CS1_GO          CR_GO           /* go */
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
#define CS1_DVA         0004000         /* drive avail NI */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)

/* u3  low */
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

/* u3 high */
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

/* RPMR - 03 - maintenace register */

/* RPAS - 04 - attention summary */

#define AS_U0           0000001         /* unit 0 flag */

#define DA           u4
/* u4 high */
/* RPDC - 05 - desired sector */

#define DA_V_SC         16              /* sector pos */
#define DA_M_SC         077             /* sector mask */
#define DA_V_SF         24              /* track pos */
#define DA_M_SF         077             /* track mask */
#define DA_MBZ          0140300
#define GET_SC(x)       (((x) >> DA_V_SC) & DA_M_SC)
#define GET_SF(x)       (((x) >> DA_V_SF) & DA_M_SF)

/* RPDT - 06 - drive type */

/* RPLA - 07 - look ahead register */

#define LA_V_SC         6                               /* sector pos */

/* RPER2 - 10 - error status 2 - drive unsafe conditions - unimplemented */
/* us10 */
/* RPOF  - 11 - offset register */
/* u4 low */
/* RPDC  - 12 - desired cylinder */
#define DC_V_CY         0                               /* cylinder pos */
#define DC_M_CY         01777                           /* cylinder mask */
#define DC_MBZ          0176000
#define GET_CY(x)       (((x) >> DC_V_CY) & DC_M_CY)
#define GET_DA(c,d)     ((((GET_CY (c) * rp_drv_tab[d].surf) + GET_SF (c)) \
                                       * rp_drv_tab[d].sect) + GET_SC (c))
#define CCYL          u5
/* u5 low */
/* RPCC  - 13 - current cylinder */

/* RPSN  - 14 - serial number */
/* RPER3 - 15 - error status 3 - more unsafe conditions - unimplemented */
#define ERR2          us9
/* us9 */
#define ERR3          us10

/* RPDB - 176722 - data buffer */


#define OF_HCI          0002000                         /* hdr cmp inh NI */
#define OF_ECI          0004000                         /* ECC inhibit NI */
#define OF_F22          0010000                         /* format NI */
#define OF_MBZ          0161400

#define DATAPTR       u6
/* RPEC1 - 16 - ECC status 1 - unimplemented */
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


struct df10   rp_df10[NUM_DEVS_RP];
int           rp_xfer_drive[NUM_DEVS_RP];
uint64        rp_buf[NUM_DEVS_RP][RP_NUMWD];
int           rp_reg[NUM_DEVS_RP];
int           rp_ivect[NUM_DEVS_RP];
int           rp_imode[NUM_DEVS_RP];
int           rp_drive[NUM_DEVS_RP];
int           rp_rae[NUM_DEVS_RP];
int           rp_attn[NUM_DEVS_RP];
extern int    readin_flag;

t_stat        rp_devio(uint32 dev, uint64 *data);
int           rp_devirq(uint32 dev, int addr);
void          rp_write(int ctlr, int unit, int reg, uint32 data);
uint32        rp_read(int ctlr, int unit, int reg);
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


UNIT                rp_unit[] = {
/* Controller 1 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(0), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(0), RP06_SIZE) },
#if (NUM_DEVS_RP > 1)
/* Controller 2 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(1), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(1), RP06_SIZE) },
#if (NUM_DEVS_RP > 2)
/* Controller 3 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(2), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(2), RP06_SIZE) },
#if (NUM_DEVS_RP > 3)
/* Controller 4 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(3), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE)+CNTRL(3), RP06_SIZE) },
#endif
#endif
#endif
};

DIB rp_dib[] = {
    {RH10_DEV, 1, &rp_devio, &rp_devirq},
    {RH10_DEV, 1, &rp_devio, &rp_devirq},
    {RH10_DEV, 1, &rp_devio, &rp_devirq},
    {RH10_DEV, 1, &rp_devio, &rp_devirq}};

MTAB                rp_mod[] = {
    {UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL},
    {UNIT_DTYPE, (RP07_DTYPE << UNIT_V_DTYPE), "RP07", "RP07", &rp_set_type },
    {UNIT_DTYPE, (RP06_DTYPE << UNIT_V_DTYPE), "RP06", "RP06", &rp_set_type },
    {UNIT_DTYPE, (RP04_DTYPE << UNIT_V_DTYPE), "RP04", "RP04", &rp_set_type },
    {0}
};

REG                 rpa_reg[] = {
    {ORDATA(IVECT, rp_ivect[0], 18)},
    {FLDATA(IMODE, rp_imode[0], 0)},
    {ORDATA(XFER, rp_xfer_drive[0], 3), REG_HRO},
    {ORDATA(DRIVE, rp_drive[0], 3), REG_HRO},
    {ORDATA(REG, rp_reg[0], 6), REG_RO},
    {ORDATA(RAE, rp_rae[0], 8), REG_RO},
    {ORDATA(ATTN, rp_attn[0], 8), REG_RO},
    {FLDATA(READIN, readin_flag, 0), REG_HRO},
    {ORDATA(STATUS, rp_df10[0].status, 18), REG_RO},
    {ORDATA(CIA, rp_df10[0].cia, 18)},
    {ORDATA(CCW, rp_df10[0].ccw, 18)},
    {ORDATA(WCR, rp_df10[0].wcr, 18)},
    {ORDATA(CDA, rp_df10[0].cda, 18)},
    {ORDATA(DEVNUM, rp_df10[0].devnum, 9), REG_HRO},
    {ORDATA(BUF, rp_df10[0].buf, 36), REG_HRO},
    {ORDATA(NXM, rp_df10[0].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, rp_df10[0].ccw_comp, 8), REG_HRO},
    {BRDATA(BUFF, &rp_buf[0][0], 16, 64, RP_NUMWD), REG_HRO},
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
    {ORDATA(IVECT, rp_ivect[1], 18)},
    {FLDATA(IMODE, rp_imode[1], 0)},
    {ORDATA(XFER, rp_xfer_drive[1], 3), REG_HRO},
    {ORDATA(DRIVE, rp_drive[1], 3), REG_HRO},
    {ORDATA(REG, rp_reg[1], 6), REG_RO},
    {ORDATA(RAE, rp_rae[1], 8), REG_RO},
    {ORDATA(ATTN, rp_attn[1], 8), REG_RO},
    {ORDATA(STATUS, rp_df10[1].status, 18), REG_RO},
    {ORDATA(CIA, rp_df10[1].cia, 18)},
    {ORDATA(CCW, rp_df10[1].ccw, 18)},
    {ORDATA(WCR, rp_df10[1].wcr, 18)},
    {ORDATA(CDA, rp_df10[1].cda, 18)},
    {ORDATA(DEVNUM, rp_df10[1].devnum, 9), REG_HRO},
    {ORDATA(BUF, rp_df10[1].buf, 36), REG_HRO},
    {ORDATA(NXM, rp_df10[1].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, rp_df10[1].ccw_comp, 8), REG_HRO},
    {BRDATA(BUFF, &rp_buf[1][0], 16, 64, RP_NUMWD), REG_HRO},
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
    {ORDATA(IVECT, rp_ivect[2], 18)},
    {FLDATA(IMODE, rp_imode[2], 0)},
    {ORDATA(XFER, rp_xfer_drive[2], 3), REG_HRO},
    {ORDATA(DRIVE, rp_drive[2], 3), REG_HRO},
    {ORDATA(REG, rp_reg[2], 6), REG_RO},
    {ORDATA(RAE, rp_rae[2], 8), REG_RO},
    {ORDATA(ATTN, rp_attn[2], 8), REG_RO},
    {ORDATA(STATUS, rp_df10[2].status, 18), REG_RO},
    {ORDATA(CIA, rp_df10[2].cia, 18)},
    {ORDATA(CCW, rp_df10[2].ccw, 18)},
    {ORDATA(WCR, rp_df10[2].wcr, 18)},
    {ORDATA(CDA, rp_df10[2].cda, 18)},
    {ORDATA(DEVNUM, rp_df10[2].devnum, 9), REG_HRO},
    {ORDATA(BUF, rp_df10[2].buf, 36), REG_HRO},
    {ORDATA(NXM, rp_df10[2].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, rp_df10[2].ccw_comp, 8), REG_HRO},
    {BRDATA(BUFF, &rp_buf[2][0], 16, 64, RP_NUMWD), REG_HRO},
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
    {ORDATA(IVECT, rp_ivect[3], 18)},
    {FLDATA(IMODE, rp_imode[3], 0)},
    {ORDATA(XFER, rp_xfer_drive[3], 3), REG_HRO},
    {ORDATA(DRIVE, rp_drive[3], 3), REG_HRO},
    {ORDATA(REG, rp_reg[3], 6), REG_RO},
    {ORDATA(RAE, rp_rae[3], 8), REG_RO},
    {ORDATA(ATTN, rp_attn[3], 8), REG_RO},
    {ORDATA(STATUS, rp_df10[3].status, 18), REG_RO},
    {ORDATA(CIA, rp_df10[3].cia, 18)},
    {ORDATA(CCW, rp_df10[3].ccw, 18)},
    {ORDATA(WCR, rp_df10[3].wcr, 18)},
    {ORDATA(CDA, rp_df10[3].cda, 18)},
    {ORDATA(DEVNUM, rp_df10[3].devnum, 9), REG_HRO},
    {ORDATA(BUF, rp_df10[3].buf, 36), REG_HRO},
    {ORDATA(NXM, rp_df10[3].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, rp_df10[3].ccw_comp, 8), REG_HRO},
    {BRDATA(BUFF, &rp_buf[3][0], 16, 64, RP_NUMWD), REG_HRO},
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


t_stat rp_devio(uint32 dev, uint64 *data) {
     int            ctlr = -1;
     DEVICE        *dptr = NULL;
     struct df10   *df10;
     int            drive;

     for (drive = 0; rh[drive].dev_num != 0; drive++) {
        if (rh[drive].dev_num == (dev & 0774)) {
            dptr = rh[drive].dev;
            break;
        }
     }
     if (dptr == NULL)
        return SCPE_OK;
     ctlr = GET_CNTRL(dptr->units[0].flags);
     df10 = &rp_df10[ctlr];
     df10->devnum = dev;
     switch(dev & 3) {
     case CONI:
        *data = df10->status & ~(IADR_ATTN|IARD_RAE);
        if (rp_attn[ctlr] != 0 && (df10->status & IADR_ATTN))
           *data |= IADR_ATTN;
        if (rp_rae[ctlr] != 0 && (df10->status & IARD_RAE))
           *data |= IARD_RAE;
#if KI_22BIT
        *data |= B22_FLAG;
#endif
        sim_debug(DEBUG_CONI, dptr, "RP %03o CONI %06o PC=%o %o\n",
               dev, (uint32)*data, PC, rp_attn[ctlr]);
        return SCPE_OK;

     case CONO:
         clr_interrupt(dev);
         df10->status &= ~(07LL|IADR_ATTN|IARD_RAE);
         df10->status |= *data & (07LL|IADR_ATTN|IARD_RAE);
         /* Clear flags */
         if (*data & CONT_RESET) {
            UNIT *uptr=dptr->units;
            for(drive = 0; drive < NUM_UNITS_RP; drive++, uptr++) {
               uptr->CMD &= DS_MOL|DS_WRL|DS_DPR|DS_DRY|DS_VV|076;
               uptr->DA &= 003400177777;
               uptr->CCYL &= 0177777;
               uptr->ERR2 = 0;
               uptr->ERR3 = 0;
            }
         }
         if (*data & (DBPE_CLR|DR_EXC_CLR|CHN_CLR))
            df10->status &= ~(*data & (DBPE_CLR|DR_EXC_CLR|CHN_CLR));
         if (*data & OVER_CLR)
            df10->status &= ~(DTC_OVER);
         if (*data & CBOV_CLR)
            df10->status &= ~(DIB_CBOV);
         if (*data & CXR_ILC)
            df10->status &= ~(CXR_ILFC|CXR_SD_RAE);
         if (*data & WRT_CW)
            df10_writecw(df10);
         if (*data & PI_ENABLE)
            df10->status &= ~PI_ENABLE;
         if (df10->status & PI_ENABLE)
            set_interrupt(dev, df10->status);
         if ((df10->status & IADR_ATTN) != 0 && rp_attn[ctlr] != 0)
            set_interrupt(dev, df10->status);
         sim_debug(DEBUG_CONO, dptr, "RP %03o CONO %06o %d PC=%06o %06o\n",
               dev, (uint32)*data, ctlr, PC, df10->status);
         return SCPE_OK;

     case DATAI:
        *data = 0;
        if (df10->status & BUSY && rp_reg[ctlr] != 04) {
            df10->status |= CC_CHAN_ACT;
            return SCPE_OK;
        }
        if (rp_reg[ctlr] == 040) {
              *data = (uint64)(rp_read(ctlr, rp_drive[ctlr], 0) & 077);
              *data |= ((uint64)(df10->cia)) << 6;
              *data |= ((uint64)(rp_xfer_drive[ctlr])) << 18;
        } else if (rp_reg[ctlr] == 044) {
              *data = (uint64)rp_ivect[ctlr];
              if (rp_imode[ctlr])
                *data |= IRQ_KI10;
              else
                *data |= IRQ_KA10;
        } else if (rp_reg[ctlr] == 054) {
                *data = (uint64)(rp_rae[ctlr]);
        } else if ((rp_reg[ctlr] & 040) == 0) {
              int parity;
              *data = (uint64)(rp_read(ctlr, rp_drive[ctlr], rp_reg[ctlr]) & 0177777);
              parity = (int)((*data >> 8) ^ *data);
              parity = (parity >> 4) ^ parity;
              parity = (parity >> 2) ^ parity;
              parity = ((parity >> 1) ^ parity) & 1;
              *data |= ((uint64)(parity ^ 1)) << 17;
              *data |= ((uint64)(rp_drive[ctlr])) << 18;
        }
        *data |= ((uint64)(rp_reg[ctlr])) << 30;
        sim_debug(DEBUG_DATAIO, dptr, "RP %03o DATI %012llo, %d %d PC=%06o\n",
                    dev, *data, ctlr, rp_drive[ctlr], PC);
        return SCPE_OK;

     case DATAO:
         sim_debug(DEBUG_DATAIO, dptr, "RP %03o DATO %012llo, %d PC=%06o %06o\n",
                    dev, *data, ctlr, PC, df10->status);
         rp_reg[ctlr] = ((int)(*data >> 30)) & 077;
         if (rp_reg[ctlr] < 040 && rp_reg[ctlr] != 04) {
            rp_drive[ctlr] = (int)(*data >> 18) & 07;
         }
         if (*data & LOAD_REG) {
             if (rp_reg[ctlr] == 040) {
                if ((*data & 1) == 0) {
                   return SCPE_OK;
                }

                if (df10->status & BUSY) {
                    df10->status |= CC_CHAN_ACT;
                    return SCPE_OK;
                }

                df10->status &= ~(1 << df10->ccw_comp);
                df10->status &= ~PI_ENABLE;
                if (((*data >> 1) & 037) < FNC_XFER) {
                   df10->status |= CXR_ILC;
                   df10_setirq(df10);
                   sim_debug(DEBUG_DATAIO, dptr,
                       "RP %03o command abort %012llo, %d[%d] PC=%06o %06o\n",
                       dev, *data, ctlr, rp_drive[ctlr], PC, df10->status);
                   return SCPE_OK;
                }
                /* Start command */
                df10_setup(df10, (uint32)(*data >> 6));
                rp_xfer_drive[ctlr] = (int)(*data >> 18) & 07;
                rp_write(ctlr, rp_drive[ctlr], 0, (uint32)(*data & 077));
                sim_debug(DEBUG_DATAIO, dptr,
                    "RP %03o command %012llo, %d[%d] PC=%06o %06o\n",
                    dev, *data, ctlr, rp_drive[ctlr], PC, df10->status);
             } else if (rp_reg[ctlr] == 044) {
                /* Set KI10 Irq vector */
                rp_ivect[ctlr] = (int)(*data & IRQ_VECT);
                rp_imode[ctlr] = (*data & IRQ_KI10) != 0;
             } else if (rp_reg[ctlr] == 050) {
                ;    /* Diagnostic access to mass bus. */
             } else if (rp_reg[ctlr] == 054) {
                /* clear flags */
                rp_rae[ctlr] &= ~(*data & 0377);
                if (rp_rae[ctlr] == 0)
                    clr_interrupt(dev);
             } else if ((rp_reg[ctlr] & 040) == 0) {
                rp_drive[ctlr] = (int)(*data >> 18) & 07;
                /* Check if access error */
                if (rp_rae[ctlr] & (1 << rp_drive[ctlr])) {
                    return SCPE_OK;
                }
                rp_write(ctlr, rp_drive[ctlr], rp_reg[ctlr] & 037,
                        (int)(*data & 0777777));
             }
         }
         return SCPE_OK;
    }
    return SCPE_OK; /* Unreached */
}

/* Handle KI and KL style interrupt vectors */
int
rp_devirq(uint32 dev, int addr) {
    DEVICE        *dptr = NULL;
    int            drive;

    for (drive = 0; rh[drive].dev_num != 0; drive++) {
       if (rh[drive].dev_num == (dev & 0774)) {
           dptr = rh[drive].dev;
           break;
       }
    }
    if (dptr != NULL) {
        drive = GET_CNTRL(dptr->units[0].flags);
        return (rp_imode[drive] ? rp_ivect[drive] : addr);
    }
    return  addr;
}

void
rp_write(int ctlr, int unit, int reg, uint32 data) {
    int            i;
    DEVICE        *dptr = rp_devs[ctlr];
    UNIT          *uptr = &dptr->units[unit];
    struct df10   *df10 = &rp_df10[ctlr];
    int            dtype = GET_DTYPE(uptr->flags);

    if ((uptr->CMD & CR_GO) && reg != 04) {
        uptr->CMD |= (ER1_RMR << 16)|DS_ERR;
        return;
    }
    switch(reg) {
    case  000:  /* control */
        sim_debug(DEBUG_DETAIL, dptr, "RP%o %d Status=%06o\n", unit, ctlr, uptr->CMD);
        /* Set if drive not writable */
        if (uptr->flags & UNIT_WLK)
           uptr->CMD |= DS_WRL;
        /* If drive not ready don't do anything */
        if ((uptr->CMD & DS_DRY) == 0) {
           uptr->CMD |= (ER1_RMR << 16)|DS_ERR;
           sim_debug(DEBUG_DETAIL, dptr, "RP%o %d not ready\n", unit, ctlr);
           return;
        }
        /* Check if GO bit set */
        if ((data & 1) == 0) {
           uptr->CMD &= ~076;
           uptr->CMD |= data & 076;
           sim_debug(DEBUG_DETAIL, dptr, "RP%o %d no go\n", unit, ctlr);
           return;                           /* No, nop */
        }
        uptr->CMD &= DS_ATA|DS_VV|DS_DPR|DS_MOL|DS_WRL;
        uptr->CMD |= data & 076;
        switch (GET_FNC(data)) {
        case FNC_NOP:
            uptr->CMD |= DS_DRY;
            break;

        case FNC_RECAL:                       /* recalibrate */
            uptr->DA &= ~0177777;
             /* Fall through */

        case FNC_RETURN:                      /* return to center */
        case FNC_OFFSET:                      /* offset */
        case FNC_UNLOAD:                      /* unload */
            uptr->CMD &= ~DS_OFF;
             /* Fall through */

        case FNC_SEARCH:                      /* search */
        case FNC_SEEK:                        /* seek */
        case FNC_WCHK:                        /* write check */
        case FNC_WRITE:                       /* write */
        case FNC_WRITEH:                      /* write w/ headers */
        case FNC_READ:                        /* read */
        case FNC_READH:                       /* read w/ headers */
            uptr->CMD |= DS_PIP;

            if (GET_CY(uptr->DA) >= rp_drv_tab[dtype].cyl ||
                GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect ||
                GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) {
                rp_attn[ctlr] &= ~(1<<unit);
                uptr->CMD |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
                uptr->CMD &= ~DS_PIP;
                df10->status &= ~BUSY;
                if ((df10->status & IADR_ATTN) != 0 && rp_attn[ctlr] != 0)
                    df10_setirq(df10);
                break;
            }

            uptr->CMD |= CR_GO;
            CLR_BUF(uptr);
            uptr->DATAPTR = 0;
            break;


        case FNC_DCLR:                        /* drive clear */
            uptr->CMD |= DS_DRY;
            uptr->CMD &= ~(DS_ATA|CR_GO);
            uptr->DA &= 003400177777;
            uptr->CCYL &= 0177777;
            uptr->ERR2 = 0;
            uptr->ERR3 = 0;
            rp_attn[ctlr] &= ~(1<<unit);
            if ((df10->status & IADR_ATTN) != 0 && rp_attn[ctlr] != 0)
                df10_setirq(df10);
            break;

        case FNC_PRESET:                      /* read-in preset */
            uptr->DA = 0;
            uptr->CCYL &= 0177777;
            uptr->CMD &= ~DS_OFF;
             /* Fall through */

        case FNC_RELEASE:                     /* port release */
        case FNC_PACK:                        /* pack acknowledge */
            if ((uptr->flags & UNIT_ATT) != 0)
                uptr->CMD |= DS_VV;
            uptr->CMD |= DS_DRY;
            if ((df10->status & IADR_ATTN) != 0 && rp_attn[ctlr] != 0)
                df10_setirq(df10);
            break;

        default:
            uptr->CMD |= DS_DRY|DS_ERR|DS_ATA;
            uptr->CMD |= (ER1_ILF << 16);
            rp_attn[ctlr] |= (1<<unit);
            if ((df10->status & IADR_ATTN) != 0 && rp_attn[ctlr] != 0)
                 df10_setirq(df10);
        }
        if (uptr->CMD & CR_GO)
            sim_activate(uptr, 1000);
        clr_interrupt(df10->devnum);
        if ((df10->status & (IADR_ATTN|BUSY)) == IADR_ATTN && rp_attn[ctlr] != 0)
             df10_setirq(df10);
        sim_debug(DEBUG_DETAIL, dptr, "RP%o AStatus=%06o\n", unit, uptr->CMD);
        return;
    case  001:  /* status */
        break;
    case  002:  /* error register 1 */
        uptr->CMD &= 0177777;
        uptr->CMD |= data << 16;
        uptr->CMD &= ~DS_ERR;
        if ((((uptr->CMD >> 16) & 0177777) | uptr->ERR2 | uptr->ERR3)  != 0)
           uptr->CMD |= DS_ERR;
        break;
    case  003:  /* maintenance */
        break;
    case  004:  /* atten summary */
        for (i = 0; i < 8; i++) {
            if (data & (1<<i)) {
                rp_unit[(ctlr * 8) + i].CMD &= ~DS_ATA;
                rp_attn[ctlr] &= ~(1<<i);
            }
        }
        if (rp_attn[ctlr] != 0)
            df10_setirq(df10);
        else
            clr_interrupt(df10->devnum);
        break;
    case  005:  /* sector/track */
        uptr->DA &= 0177777;
        uptr->DA |= data << 16;
        break;
    case  014:  /* error register 2 */
        uptr->ERR2 = data;
        uptr->CMD &= ~DS_ERR;
        if ((((uptr->CMD >> 16) & 0177777) | uptr->ERR2 | uptr->ERR3)  != 0)
           uptr->CMD |= DS_ERR;
        break;
    case  006:  /* drive type */
    case  007:  /* look ahead */
        break;
    case  011:  /* offset */
        uptr->CCYL &= 0177777;
        uptr->CCYL |= data << 16;
        break;
    case  012:  /* desired cylinder */
        uptr->DA &= ~0177777;
        uptr->DA |= data;
        break;
    case  015:  /* error register 3 */
        uptr->ERR3 = data;
        uptr->CMD &= ~DS_ERR;
        if ((((uptr->CMD >> 16) & 0177777) | uptr->ERR2 | uptr->ERR3)  != 0)
           uptr->CMD |= DS_ERR;
        break;
    case  013:  /* current cylinder */
    case  010:  /* serial no */
    case  016:  /* ecc position */
    case  017:  /* ecc pattern */
        break;
    default:
        uptr->CMD |= (ER1_ILR<<16)|DS_ERR;
        rp_rae[ctlr] &= ~(1<<unit);
    }
}

uint32
rp_read(int ctlr, int unit, int reg) {
    DEVICE        *dptr = rp_devs[ctlr];
    UNIT          *uptr = &dptr->units[unit];
    struct df10   *df10;
    uint32        temp = 0;
    int           i;

    if ((uptr->flags & UNIT_ATT) == 0 && reg != 04) {    /* not attached? */
        return 0;
    }
    switch(reg) {
    case  000:  /* control */
        df10 = &rp_df10[ctlr];
        temp = uptr->CMD & 076;
        if (uptr->flags & UNIT_ATT)
           temp |= CS1_DVA;
        if (df10->status & BUSY || uptr->CMD & CR_GO)
           temp |= CS1_GO;
        break;
    case  001:  /* status */
        temp = uptr->CMD & 0177700;
        break;
    case  002:  /* error register 1 */
        temp = (uptr->CMD >> 16) & 0177777;
        break;
    case  003:  /* maintenance */
        break;
    case  004:  /* atten summary */
        for (i = 0; i < 8; i++) {
            if (rp_unit[(ctlr * 8) + i].CMD & DS_ATA) {
                temp |= 1 << i;
            }
        }
        break;
    case  005:  /* sector/track */
        temp = (uptr->DA >> 16) & 0177777;
        break;
    case  006:  /* drive type */
        temp = rp_drv_tab[GET_DTYPE(uptr->flags)].devtype;
        break;
    case  011:  /* offset */
        temp = (uptr->CCYL >> 16) & 0177777;
        break;
    case  012:  /* desired cylinder */
        temp = uptr->DA & 0177777;
        break;
    case  013:  /* current cylinder */
        temp = uptr->CCYL & 0177777;
        break;
    case  010:  /* serial no */
        temp = (020 * ctlr) + (unit + 1);
        break;
    case  014:  /* error register 2 */
        temp = uptr->ERR2;
        break;
    case  015:  /* error register 3 */
        temp = uptr->ERR3;
        break;
    case  007:  /* look ahead */
    case  016:  /* ecc position */
    case  017:  /* ecc pattern */
        break;
    default:
        uptr->CMD |= (ER1_ILR<<16);
        rp_rae[ctlr] &= ~(1<<unit);
    }
    return temp;
}


t_stat rp_svc (UNIT *uptr)
{
    int          dtype = GET_DTYPE(uptr->flags);
    int          ctlr = GET_CNTRL(uptr->flags);
    int          unit;
    DEVICE      *dptr;
    struct df10 *df;
    int          cyl = GET_CY(uptr->DA);
    int          diff, da;
    t_stat       r;

    /* Find dptr, and df10 */
    dptr = rp_devs[ctlr];
    unit = uptr - dptr->units;
    df = &rp_df10[ctlr];
    if ((uptr->flags & UNIT_ATT) == 0) {                 /* not attached? */
        uptr->CMD |= (ER1_UNS << 16) | DS_ATA|DS_ERR;     /* set drive error */
        if (GET_FNC(uptr->CMD) >= FNC_XFER) {             /* xfr? set done */
           df->status &= ~BUSY;
           df10_setirq(df);
        }
        return (SCPE_OK);
    }

    /* Check if seeking */
    if (uptr->CMD & DS_PIP) {
        sim_debug(DEBUG_DETAIL, dptr, "RP%o seek %d %d\n", unit, cyl, uptr->CCYL);
        if (cyl >= rp_drv_tab[dtype].cyl) {
            uptr->CMD &= ~DS_PIP;
            uptr->CMD |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
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
            uptr->CMD &= ~DS_PIP;
            uptr->DATAPTR = 0;
        }
    }

    switch (GET_FNC(uptr->CMD)) {
    case FNC_NOP:
    case FNC_DCLR:                       /* drive clear */
    case FNC_RELEASE:                    /* port release */
    case FNC_PACK:                       /* pack acknowledge */
        break;
    case FNC_UNLOAD:                     /* unload */
        rp_detach(uptr);
        /* Fall through */
    case FNC_OFFSET:                     /* offset */
        uptr->CMD |= DS_OFF;
        /* Fall through */
    case FNC_RETURN:                     /* return to center */
    case FNC_PRESET:                     /* read-in preset */
    case FNC_RECAL:                      /* recalibrate */
    case FNC_SEEK:                       /* seek */
        if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect ||
            GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf)
            uptr->CMD |= (ER1_IAE << 16)|DS_ERR;
        rp_attn[ctlr] |= 1<<unit;
        uptr->CMD |= DS_DRY|DS_ATA;
        uptr->CMD &= ~CR_GO;
        if ((df->status & (IADR_ATTN|BUSY)) == IADR_ATTN)
            df10_setirq(df);
        sim_debug(DEBUG_DETAIL, dptr, "RP%o seekdone %d %o\n", unit, cyl, uptr->CMD);
        break;

    case FNC_SEARCH:                     /* search */
        if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect ||
            GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf)
            uptr->CMD |= (ER1_IAE << 16)|DS_ERR;
        rp_attn[ctlr] |= 1<<unit;
        uptr->CMD |= DS_DRY|DS_ATA;
        uptr->CMD &= ~CR_GO;
        if ((df->status & (IADR_ATTN|BUSY)) == IADR_ATTN)
            df10_setirq(df);
        sim_debug(DEBUG_DETAIL, dptr, "RP%o searchdone %d %o\n", unit, cyl, uptr->CMD);
        break;

    case FNC_READ:                       /* read */
    case FNC_READH:                      /* read w/ headers */
    case FNC_WCHK:                       /* write check */
        if (uptr->CMD & DS_ERR) {
            sim_debug(DEBUG_DETAIL, dptr, "RP%o read error\n", unit);
            goto rd_end;
        }

        if (BUF_EMPTY(uptr)) {
            int wc;

            if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect ||
                GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) {
                uptr->CMD |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
                uptr->CMD &= ~CR_GO;
                df10_finish_op(df, 0);
                sim_debug(DEBUG_DETAIL, dptr, "RP%o readx done\n", unit);
                return SCPE_OK;
            }
            sim_debug(DEBUG_DETAIL, dptr, "RP%o read (%d,%d,%d)\n", unit, cyl,
                   GET_SF(uptr->DA), GET_SC(uptr->DA));
            da = GET_DA(uptr->DA, dtype) * RP_NUMWD;
            (void)sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
            wc = sim_fread (&rp_buf[ctlr][0], sizeof(uint64), RP_NUMWD,
                                uptr->fileref);
            while (wc < RP_NUMWD)
                rp_buf[ctlr][wc++] = 0;
            uptr->hwmark = RP_NUMWD;
            uptr->DATAPTR = 0;
            /* On read headers, transfer 2 words to start */
            if (GET_FNC(uptr->CMD) == FNC_READH) {
                df->buf = (((uint64)cyl) << 18) | 
                         ((uint64)((GET_SF(uptr->DA) << 8) | GET_SF(uptr->DA)));
                sim_debug(DEBUG_DATA, dptr, "RP%o read word h1 %012llo %09o %06o\n",
                   unit, df->buf, df->cda, df->wcr);
                if (df10_write(df) == 0)
                    goto rd_end;
                df->buf = ((uint64)((020 * ctlr) + (unit + 1)) << 18) | (uint64)(unit);
                sim_debug(DEBUG_DATA, dptr, "RP%o read word h2 %012llo %09o %06o\n",
                   unit, df->buf, df->cda, df->wcr);
                if (df10_write(df) == 0)
                    goto rd_end;
            }
        }

        df->buf = rp_buf[ctlr][uptr->DATAPTR++];
        sim_debug(DEBUG_DATA, dptr, "RP%o read word %d %012llo %09o %06o\n",
                   unit, uptr->DATAPTR, df->buf, df->cda, df->wcr);
        if (df10_write(df)) {
            if (uptr->DATAPTR == RP_NUMWD) {
                /* Increment to next sector. Set Last Sector */
                uptr->DATAPTR = 0;
                CLR_BUF(uptr);
                uptr->DA += 1 << DA_V_SC;
                if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect) {
                    uptr->DA &= (DA_M_SF << DA_V_SF) | (DC_M_CY << DC_V_CY);
                    uptr->DA += 1 << DA_V_SF;
                    if (GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) {
                         uptr->DA &= (DC_M_CY << DC_V_CY);
                         uptr->DA += 1 << DC_V_CY;
                         uptr->CMD |= DS_PIP;
                    }
                }
            }
            sim_activate(uptr, 50);
        } else {
rd_end:
            sim_debug(DEBUG_DETAIL, dptr, "RP%o read done\n", unit);
            uptr->CMD |= DS_DRY;
            uptr->CMD &= ~CR_GO;
            df10_finish_op(df, 0);
            return SCPE_OK;
        }
        break;

    case FNC_WRITE:                      /* write */
    case FNC_WRITEH:                     /* write w/ headers */
        if (uptr->CMD & DS_ERR) {
            sim_debug(DEBUG_DETAIL, dptr, "RP%o read error\n", unit);
            goto wr_end;
        }

        if (BUF_EMPTY(uptr)) {
            if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect ||
                GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) {
                uptr->CMD |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
                uptr->CMD &= ~CR_GO;
                df10_finish_op(df, 0);
                sim_debug(DEBUG_DETAIL, dptr, "RP%o writex done\n", unit);
                return SCPE_OK;
            }
            /* On Write headers, transfer 2 words to start */
            if (GET_FNC(uptr->CMD) == FNC_WRITEH) {
                if (df10_read(df) == 0)
                    goto wr_end;
                sim_debug(DEBUG_DATA, dptr, "RP%o write word h1 %012llo %06o\n",
                      unit, df->buf, df->wcr);
                if (df10_read(df) == 0)
                    goto wr_end;
                sim_debug(DEBUG_DATA, dptr, "RP%o write word h2 %012llo %06o\n",
                      unit, df->buf, df->wcr);
            }
            uptr->DATAPTR = 0;
            uptr->hwmark = 0;
        }
        r = df10_read(df);
        sim_debug(DEBUG_DATA, dptr, "RP%o write word %d %012llo %06o\n",
                      unit, uptr->DATAPTR, df->buf, df->wcr);
        rp_buf[ctlr][uptr->DATAPTR++] = df->buf;
        if (r == 0 || uptr->DATAPTR == RP_NUMWD) {
            while (uptr->DATAPTR < RP_NUMWD)
                rp_buf[ctlr][uptr->DATAPTR++] = 0;
            sim_debug(DEBUG_DETAIL, dptr, "RP%o write (%d,%d,%d)\n", unit, cyl,
                   GET_SF(uptr->DA), GET_SC(uptr->DA));
            da = GET_DA(uptr->DA, dtype) * RP_NUMWD;
            (void)sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
            (void)sim_fwrite (&rp_buf[ctlr][0], sizeof(uint64), RP_NUMWD,
                                uptr->fileref);
            uptr->DATAPTR = 0;
            CLR_BUF(uptr);
            if (r) {
                uptr->DA += 1 << DA_V_SC;
                if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect) {
                    uptr->DA &= (DA_M_SF << DA_V_SF) | (DC_M_CY << DC_V_CY);
                    uptr->DA += 1 << DA_V_SF;
                    if (GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) {
                         uptr->DA &= (DC_M_CY << DC_V_CY);
                         uptr->DA += 1 << DC_V_CY;
                         uptr->CMD |= DS_PIP;
                    }
                }
             }
        }
        if (r) {
            sim_activate(uptr, 50);
        } else {
wr_end:
            sim_debug(DEBUG_DETAIL, dptr, "RP%o write done\n", unit);
            uptr->CMD |= DS_DRY;
            uptr->CMD &= ~CR_GO;
            df10_finish_op(df, 0);
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
    int ctlr;
    for (ctlr = 0; ctlr < NUM_DEVS_RP; ctlr++) {
        rp_df10[ctlr].devnum = rp_dib[ctlr].dev_num;
        rp_df10[ctlr].nxmerr = 19;
        rp_df10[ctlr].ccw_comp = 14;
        rp_df10[ctlr].status = 0;
        rp_attn[ctlr] = 0;
        rp_rae[ctlr] = 0;
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
rp_boot(int32 unit_num, DEVICE * rptr)
{
    UNIT         *uptr = &rptr->units[unit_num];
    int           ctlr = GET_CNTRL(uptr->flags);
    DEVICE       *dptr;
    struct df10  *df;
    uint32        addr;
    uint32        ptr = 0;
    uint64        word;
    int           wc;

    df = &rp_df10[ctlr];
    dptr = rp_devs[ctlr];
    (void)sim_fseek(uptr->fileref, 0, SEEK_SET);
    (void)sim_fread (&rp_buf[0][0], sizeof(uint64), RP_NUMWD, uptr->fileref);
    uptr->CMD |= DS_VV;
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

    rp_reg[ctlr] = 040;
    rp_drive[ctlr] = uptr - dptr->units;
    df->status |= CCW_COMP_1|PI_ENABLE;
    PC = word & RMASK;
    return SCPE_OK;
}

/* Device attach */

t_stat rp_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    DEVICE *rptr;
    DIB *dib;
    int ctlr;

    uptr->capac = rp_drv_tab[GET_DTYPE (uptr->flags)].size;
    r = attach_unit (uptr, cptr);
    if (r != SCPE_OK)
        return r;
    rptr = find_dev_from_unit(uptr);
    if (rptr == 0)
        return SCPE_OK;
    dib = (DIB *) rptr->ctxt;
    ctlr = dib->dev_num & 014;
    uptr->DA = 0;
    uptr->CMD &= ~DS_VV;
    uptr->CMD |= DS_DPR|DS_MOL|DS_DRY;
    if (uptr->flags & UNIT_WLK)
         uptr->CMD |= DS_WRL;
    rp_df10[ctlr].status |= PI_ENABLE;
    set_interrupt(dib->dev_num, rp_df10[ctlr].status);
    return SCPE_OK;
}

/* Device detach */

t_stat rp_detach (UNIT *uptr)
{
    if (!(uptr->flags & UNIT_ATT))                          /* attached? */
        return SCPE_OK;
    if (sim_is_active (uptr))                              /* unit active? */
        sim_cancel (uptr);                                  /* cancel operation */
    uptr->CMD &= ~(DS_VV|DS_WRL|DS_DPR|DS_DRY);
    return detach_unit (uptr);
}

t_stat rp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RP04/05/06/07 Disk Pack Drives (RP)\n\n");
fprintf (st, "The RP controller implements the Massbus family of large disk drives.  RP\n");
fprintf (st, "options include the ability to set units write enabled or write locked, to\n");
fprintf (st, "set the drive type to one of six disk types or autosize, and to write a DEC\n");
fprintf (st, "standard 044 compliant bad block table on the last track.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The RP device supports the BOOT command.\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *rp_description (DEVICE *dptr)
{
    return "RP04/05/06/07 Massbus disk controller";
}


#endif
